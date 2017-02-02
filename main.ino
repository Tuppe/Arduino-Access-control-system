
#define BAUD 115200
#define BLEN 128 //max number of characters per each line
#define BUZZER 9
#define LED_R 6
#define LED_G 5
#define RELAY 4
#define RELAY2 3
#define LON LOW
#define LOFF HIGH
#define ECHO 2 //interrupt 1
#define TRIG A0 //transmit
#define HALLS A1 //hall sensor

char inbuf[BLEN];
char readchar;

byte gateState = 0;
long duration;
long normaldist;
volatile int relayTimer = 0; //volatile because used in timer1 interrupt
uint32_t id;
uint32_t lastid;

char* domain = "phptesting.azurewebsites.net/testi.php?getID="; //body of the server address

//init NFC reader
#include <PN532.h>
#include <SPI.h>
#define PN532_CS 10
PN532 nfc(PN532_CS);


void setup()
{

  //all Serial.stuff is used ONLY for debug
  Serial.begin(BAUD); //init serial through USB port
  
  //this is for modem communication
  Serial1.begin(BAUD); //init UART for gsm modem

  //init pins
  for (byte i = 3; i <= 9; i++)
    pinMode(i, OUTPUT); //set pins 5-9 to output

  pinMode(ECHO, INPUT);
  pinMode(TRIG, OUTPUT);
  pinMode(HALLS, INPUT);

  //turn off the signals
  digitalWrite(LED_G, LOFF);
  digitalWrite(LED_R, LOFF);
  digitalWrite(BUZZER, LOW);

  //init nfc reader
  nfc.begin();
  nfc.SAMConfig();

  init_timer();
  normaldist = get_clearance(); //init ultrasound
  clearBuffer();

  byte checkcount = 0;

  //while (!Serial); // while the serial stream is not open, do nothing

  Serial.println("Begin");
  //GSMReset();


/* initialize the GSM modem for HTTP communication. This is routine is GSM chipset specific
* For SIM800L:
* 1. 'AT+CREG?' Wait until registered to network
* 2. 'AT+SAPBR=1,1' Open a GPRS context
* 3. 'AT+CGATT=1' Perform a GPRS Attach
* 4. 'AT+CSTT="internet"' Set APN, this is service provider specific, http://wiki.androidsuomi.fi/Suomalaisten_operaattoreiden_APN_asetukset
* 5. 'AT+CIICR' Bring up wireless connection with GPRS
* 6. 'AT+HTTPINIT' Init HTTP
* 7. 'AT+HTTPPARA="CID",1' Set HTTP parameters
*/

  if (!getAT("AT+CREG?", "+CREG: 0,1", 50)) {
    GSMReset();
    selfReset();
  }

  //if (!getAT("AT+SAPBR=2,1", "+SAPBR: 1,1", 1)) //+SAPBR: 1,1 =

  checkcount += getAT("AT+SAPBR=1,1", "OK", 5);

  checkcount += getAT("AT+CGATT=1", "OK", 5);

  //if (!getAT("AT+CSTT?", "internet", 5))

  checkcount += getAT("AT+CSTT=\"internet\"", "OK", 5);

  checkcount += getAT("AT+CIICR", "OK", 5);

  checkcount += getAT("AT+HTTPINIT", "OK", 5);

  checkcount += getAT("AT+HTTPPARA=\"CID\",1", "OK", 5);

  //while (checkcount != 6)
  //  readBuffer("OK");

  getAT("AT+CNETLIGHT=0", "OK", 1); //earth hour starts now

}

void loop()
{

  id = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A);

  if (id) {
    digitalWrite(9, HIGH); //signal that card was read
    delay(100);
    digitalWrite(9, LOW);

    Serial.println(id);

    clearBuffer();
    char buf[10];
    ultoa(id, buf, 10); //convert tag ID to char
    char str[100]; //store HTTPPARA command here

    //Stitch URL to HTTP command
    strcpy (str, "AT+HTTPPARA=\"URL\",\"");
    strcat (str, domain);
    strcat (str, buf);
    strcat (str, "\"");

    getAT(str, "OK", 1); //send httppara command

    getAT("AT+HTTPACTION=0", "OK", 1);

    delay(100);

    unsigned long takeMillis = millis();

    //wait until modem responds to httprequest
    while (!readBuffer("+HTTPACTION:")) {
      delay(100);
      if (searchword("ERROR"))
        Error(1);

      if (millis() - takeMillis > 8000)
        break; //let's not get stuck here, timeout
    }

    Serial.println(getAT("AT+HTTPREAD", "+HTTPREAD:", 1));

    while (readBuffer("CONN_OK")) { //Server has responded
      delay(50);
      if (searchword("ID_OK"))
      {
        lastid = id;
        TCNT1 = 65536; //led timer
        tone(BUZZER, 2349, 700);
        digitalWrite(LED_G, LON);
        relayTimer = 0;
        gateState = 1; //open gate
        break;
      }
      else
      {
        //'card not valid' signal routine
        digitalWrite(BUZZER, HIGH);
        digitalWrite(LED_R, LON);
        delay(550);
        digitalWrite(BUZZER, LOW);
        delay(150);
        digitalWrite(BUZZER, HIGH);
        delay(700);
        digitalWrite(BUZZER, LOW);
        delay(1000);
        digitalWrite(LED_R, LOFF);
        break;
      }
    }
    id = 0;
  }

  gateLogic();

  if (Serial.available()) //send AT commands to modem from terminal
    Serial1.write(Serial.read());

  readBuffer(NULL);
}


/*
 * Function:  readBuffer
 * --------------------
 *  read serial to buffer and process row-by-row
 *  argument can be NULL if reading buffer just for debug output
 *
 *  returns: 0/1 if word found
 */
byte readBuffer(char* findword)
{
  byte count = 0; //mind the buffer length limit of datatype

  do
  {
    if (count < BLEN) {
      inbuf[count++] = Serial1.read();
      if (count > 0 && inbuf[count - 1] > 0) Serial.print(inbuf[count - 1]); //print in the debug terminal

      if ((inbuf[count - 2]) == '\r' && inbuf[count - 1] == '\n') //end of the row
        if (searchword(findword) > 0) {
          //while (Serial1.available())
          //  Serial.write(Serial1.read());

          return 1;
        }
    }
    else {
      count = 0;
      clearBuffer();
    }
    //delayMicroseconds(500); //wait for dem bits to arrive
  } while (Serial1.available());


  return 0;
}


/*
 * Function:  searchword
 * --------------------
 *  returns: word position in buffer
 */
int searchword(char* wordtofind)
{
  char* strPtr = strstr(inbuf, wordtofind); //internal strstr command finds string from string
  if (strPtr != NULL) //it returned something
    return (int)(strPtr - inbuf);
}


void selfReset() { //reset MCU
  Serial.println("Self reset!");
  delay(10);
  digitalWrite(7, HIGH);
}


void GSMReset() {
  Serial.println("GSM reset!");
  digitalWrite(8, LOW);
  delay(1);
  digitalWrite(8, HIGH);
}


void clearBuffer()
{
  for (unsigned int i = 0; i < BLEN; i++) inbuf[i] = NULL;
}


/*
 * Function:  getAT
 * --------------------
 *  sends AT command until expected response or timeout
 *
 *  returns: 0/1 if command successful
 */
byte getAT(char* command, char* response, byte trycount)
{
  unsigned long takeMillis = 0;

  for (byte i = 0; i < trycount; i++) {

    Serial1.write(command); //send AT command
    Serial1.write("\r\n");

    delay(20); //wait just a bit for response

    takeMillis = millis();
    while (!Serial1.available()) //Wait until we have something to read
      if (millis() - takeMillis > 5000) break; //timeout, try again

    if (readBuffer(response)) return 1;

    delay(10 * trycount); //wait before retry
  }

  return 0; //no response
}


//ISR for timer1 (a.k.a. "do this when timer1 overflows")
ISR(TIMER1_OVF_vect) 
{
  digitalWrite(LED_G, LOFF);
  if (gateState > 0) {
    Serial.print("relaytimer: ");
    Serial.println(relayTimer);
    Serial.print("hall read: ");
    Serial.println(digitalRead(HALLS));
    relayTimer++; //count seconds
  }
}


// initialize timer1
void init_timer() {
  
  noInterrupts(); // disable all interrupts

  //timer1 control registers
  TCCR1A = 0; //mode0
  TCCR1B = 0; //start point

  //timer1 settings
  TCCR1B |= (1 << CS12);    // 256 prescaler
  TIMSK1 |= (1 << TOIE1);   // enable timer overflow interrupt
  interrupts();             // enable all interrupts

  //CS12=256 prescaler
  //16000000/256 = 62500
  //65536/62500 = 1.048576s, we're okay with that error
}


//gets error code and handles accordingly. Expandable
void Error(byte code) {
  Serial.print("ERROR: "); //debug data
  Serial.println(code);

  switch (code) {
    case 1: //HTTP command error

      // flash red light few times so the user knows something's wrong
      for (byte i = 0; i < 3; i++) {
        digitalWrite(LED_R, digitalRead(LED_R) ^ 1);
        delay(2000);
      }
      //irrecoverable error, reset systems
      GSMReset();
      selfReset();
      break;

    case 2: //limit switch timeout

      for (byte i = 0; i < 4; i++) {
        digitalWrite(LED_R, digitalRead(LED_R) ^ 1);
        delay(500);
      }

      gateState = 0;
      relayTimer = 0;

      int oldstate = digitalRead(HALLS);
      do {
        id = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A);
        int diff = normaldist - get_clearance(); //get ultrasound measurements
        diff = abs(diff); //separate line here to avoid macro error

        Serial.println(id);
        
        if (id == lastid)
        {
          Serial.println(diff);

          if (diff > 500) {
            digitalWrite(RELAY, HIGH);
            digitalWrite(RELAY2, LOW);
          }
          else {
            digitalWrite(RELAY2, HIGH);
            digitalWrite(RELAY, LOW);
          }
        }
        else {
          digitalWrite(RELAY, LOW);
          digitalWrite(RELAY2, LOW);
        }
        id = 0;

      } while (oldstate == digitalRead(HALLS));

      break;
  }
}


long get_clearance() {
  long distancecount = 0;
  
  for (int i = 0; i < 10; i++) {
    //send pulse
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIG, LOW);
    
    duration = pulseIn(ECHO, HIGH); //get pulse

    distancecount += duration;

    delay(10);
  }
  return distancecount / 10;
}

void gateLogic() {
  /* gateState:
  * 0 stopped
  * 1 rising
  * 2 waiting
  * 3 lowering
  */

  if (gateState == 1) {
    Serial.println("GATE RISING");
    digitalWrite(RELAY, HIGH);
    while (digitalRead(HALLS) == 0)
      //timeout for reaching limit switch, 10 seconds for demo
      if (relayTimer > 10) {
        gateState = 0;
        Error(2);
        break;
      }

    digitalWrite(RELAY, LOW);
    gateState = 2;
    relayTimer = 0;
  }

  if (gateState == 2) {
    Serial.println("GATE WAITING");
    while (relayTimer < 5) { //wait 5 secs
      Serial.println(relayTimer);
    }
    gateState = 3;
    relayTimer = 0;
  }

  if (gateState == 3) {
    Serial.println("GATE LOWERING");
    while (digitalRead(HALLS) == 1) {
      int diff = normaldist - get_clearance(); //something in the way?
      diff = abs(diff); //separate line to avoid messing up abs macro

      if (diff > 500) {
        Serial.print("OBSTACLE AT:");
        Serial.println(diff);
        digitalWrite(LED_R, LON);
        digitalWrite(RELAY2, LOW);
        digitalWrite(BUZZER, HIGH);
        gateState = 0;
        delay(3000); //wait for the dude to move his damn car
      }
      else {
        //no obstacles, lower the gate
        digitalWrite(BUZZER, digitalRead(BUZZER) ^ 1); //and make some noise for attention
        digitalWrite(LED_R, LOFF);
        digitalWrite(RELAY2, HIGH);
        gateState = 2;
      }
      if (relayTimer > 10) {
        Error(2);
        break;
      }
    }
    digitalWrite(RELAY2, LOW);
    digitalWrite(BUZZER, LOW);
    
    relayTimer = 0;
    gateState = 0;
    Serial.println("GATE DONE");
  }
  Serial.println(digitalRead(HALLS));
}

/*
    for (int i = 0; i < BLEN; i++) {
      if (inbuf[i] != NULL)
        Serial.write(inbuf[i]); //write to terminal
    }
*/
