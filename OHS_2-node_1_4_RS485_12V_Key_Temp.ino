// Remote node with RS485, Power 12V, temperature(A6), iButton reader with 2 LEDs
// Board v.1.40

#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466

#include <OneWire.h>
#include <LibRS485v2.h>
#include <avr/eeprom.h> // Global configuration for in chip EEPROM

//#include <SoftwareSerial.h>
//SoftwareSerial mySerial(A4, A5); // RX, TX

#define MY_ADDRESS 1    // 0 is gateway, 15 is multicast
#define LED_GREEN  5    // iButton probe LED
#define LED_RED    4    // iButton probe LED
#define SPEAKER    7    // Speaker pin
#define DE         3    // RS 485 DE pin
#define VERSION    140  // Version of EEPROM struct
#define REG_LEN    21   // size of one conf. element
#define RS485_REPEAT 5   // repeat sending

OneWire ds(6);          // Dallas reader on pin with  4k7 pull-up rezistor
RS485_msg in_msg, out_msg; 

// Global variables
int8_t   i;
uint8_t  addr[8];             // Dallas chip
uint8_t  key[8];              // Dallas chip
uint8_t  mode = 0;
uint8_t  pos;
int8_t   iButtonRead = 0;
unsigned long previousMillis = 0;
unsigned long readerMillis = 0;
unsigned long tempMillis = 0;
unsigned long aliveMillis = 0;

// Notes and LEDs patterns
char *goodkey  = "G1,,G5,,g0,.";
char *wrongkey = "R1,,R1,,r0,.";
char *auth0    = "R5,r0,.";
char *auth1    = "R5,r0,,,,.";
char *auth2    = "R5,r0,,,,,,.";
char *auth3    = "R5,r0,,,,,,,,.";
char *p        = ".";
char *ok       = "G,g,,,,,,,,,.";
char *armed    = "R,r,,,,,,,,,.";
char *arming   = "R7,r5,R7,r0,,,,,,,,.";
char *iButton  = "G5,g0,,,,,.";
int notes[] = { NOTE_A3, NOTE_B3, NOTE_C4, NOTE_D4, NOTE_E4, NOTE_F4, NOTE_G4, NOTE_A4 };

struct config_t {
  uint16_t version;
  char     reg[REG_LEN * 2]; // Number of elements on this node
} conf;

// Float conversion
union u_tag {
    uint8_t  b[4];
    float    fval;
} u;

// Registration
void send_conf(){
  int8_t result;
  delay(MY_ADDRESS*200); // Wait some time to avoid contention
  out_msg.address = 0;
  out_msg.ack = FLAG_ACK;
  out_msg.ctrl = FLAG_DTA;
  out_msg.data_length = REG_LEN + 1; // Add 'R'
  pos = 0;
  while (pos < sizeof(conf.reg)) {
    out_msg.buffer[0] = 'R'; // Registration flag
    memcpy(&out_msg.buffer[1], &conf.reg[pos], REG_LEN);
    result = RS485.sendMsgWithAck(&out_msg, RS485_REPEAT);
    pos += REG_LEN;
  }
  // Play tones to confirm at least last send
  if (result == 1) {
    tone(SPEAKER, notes[0]);  delay(100); tone(SPEAKER, notes[7]);  delay(100); noTone(SPEAKER);
  } else {
    tone(SPEAKER, notes[7]);  delay(100); tone(SPEAKER, notes[0]);  delay(100); noTone(SPEAKER);
  }
}

// Set defaults on first time
void setDefault(){
  conf.version = VERSION;   // Change VERSION to take effect
  conf.reg[0]  = 'K';       // Key
  conf.reg[1]  = 'i';       // iButton
  conf.reg[2]  = 0;         // Local address, must be odd number
  conf.reg[3]  = B00000000; // Default setting
  conf.reg[4]  = B00011111; // Default setting, group=16, disabled
  for (uint8_t ii=0; ii < 17; ii++){ conf.reg[5+ii] = 0;} // Placeholder for name
  conf.reg[21] = 'S';       // Sensor
  conf.reg[22] = 'T';       // Temperature
  conf.reg[23] = 0;         // Local address
  conf.reg[24] = B00000000; // Default setting
  conf.reg[25] = B00011111; // Default setting, group=16, disabled
  for (uint8_t ii=0; ii < 17; ii++){ conf.reg[26+ii] = 0;}
}

void setup() {
  pinMode(DE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);  
  pinMode(A5, OUTPUT);
  pinMode(A3, OUTPUT);
  // SoftwareSerial digitalWrite(A5, LOW);
  // SoftwareSerial digitalWrite(A3, LOW);
  
  RS485.begin(19200, MY_ADDRESS);

  eeprom_read_block((void*)&conf, (void*)0, sizeof(conf)); // Read current configuration
  if (conf.version != VERSION) setDefault();
  
  delay(MY_ADDRESS * 1000);
  //send_conf(); 
 
  previousMillis = millis();
  readerMillis   = millis(); 
  tempMillis     = millis(); 
  aliveMillis    = millis() + 600000; // Do ping at start

  p = iButton; // Do beep at startup
  //mySerial.begin(19200);
}

void loop() {
  // Look for incomming transmissions
  i = RS485.readMsg(&in_msg);
  if (i == 1) {
    // Commands from gateway
    if (in_msg.ctrl == FLAG_CMD) {
      switch (in_msg.data_length) {
        case 1: send_conf(); break; // Request for registration
        case 10 ... 16 : mode = in_msg.data_length; break; // Auth. commands
        default: break;
      }
    }
    // Configuration change 
    if (in_msg.ctrl == FLAG_DTA && in_msg.buffer[0] == 'R') {
      // Replace part of conf with new data.
      pos = 0; 
      //for (uint8_t ii=0; ii < in_msg.data_length-1; ii++){ mySerial.print((char)in_msg.buffer[1+ii]); }
      while (((conf.reg[pos] != in_msg.buffer[1]) || (conf.reg[pos+1] != in_msg.buffer[2]) ||
              (conf.reg[pos+2] != in_msg.buffer[3])) && (pos < sizeof(conf.reg))) {
        pos += REG_LEN; // size of one conf. element
      }      
      if (pos < sizeof(conf.reg)) {
        //* for (uint8_t ii=0; ii < in_msg.data_length-1; ii++){ conf.reg[pos+ii]=in_msg.buffer[1+ii]; }
        memcpy(&conf.reg[pos], &in_msg.buffer[1], REG_LEN);
        // Save it to EEPROM
        conf.version = VERSION;
        eeprom_update_block((const void*)&conf, (void*)0, sizeof(conf)); // Save current configuration
        tone(SPEAKER, notes[pos/REG_LEN]);  delay(100); noTone(SPEAKER); 
      }
    }
  } // RS485 message
  
  // Tone and leds
  if ((unsigned long)(millis() - previousMillis) >= 200) {
    previousMillis = millis();  
    if (*p == '.') {
      // reset all sound and LED
      digitalWrite(LED_RED, LOW);
      digitalWrite(LED_GREEN, LOW);
      noTone(SPEAKER);
      // change the mode
      switch (mode) {
        case 10: p = arming; break;
        case 11: p = auth0; break;
        case 12: p = auth1; break;
        case 13: p = auth2; break;
        case 14: p = auth3; break;          
        case 15: p = armed; break;
        default: p = ok; break; // 0 or 16
      }
    } 
    while (*p != ',') {
      switch (*p) {
        case 'G': digitalWrite(LED_GREEN, HIGH); break;
        case 'g': digitalWrite(LED_GREEN, LOW); break;
        case 'R': digitalWrite(LED_RED, HIGH); break;
        case 'r': digitalWrite(LED_RED, LOW); break;
        case '1'...'9': tone(SPEAKER, notes[*p-49]); break;
        case '0': noTone(SPEAKER); break;  
        default: break;
      }
      p++;  
    }
    p++;
    
    // iButton
    if (iButtonRead >= 0) {
      if (!ds.search(addr)) {
        ds.reset_search();
      } else { // we have chip at reader
        // valid crc
        if ( OneWire::crc8(addr, 7) == addr[7]) {
          if (iButtonRead == 0) {
            p = iButton; // play
            readerMillis = millis();
          }
          iButtonRead++;
          memcpy(&key[0], &addr[0], sizeof(key));
        }
        ds.reset_search(); 
      } 
    }
    if ((unsigned long)(millis() - readerMillis) > 1400){
      // We have at least one good key
      if (iButtonRead > 0) {
        out_msg.address = 0;
        out_msg.ctrl = FLAG_DTA;
        out_msg.data_length = 11;
        out_msg.buffer[0] = 'K'; 
        out_msg.buffer[1] = 'i'; 
        // If iButton is held at reader or just touched
        if (iButtonRead > 4) out_msg.buffer[2] = 1; 
        else                 out_msg.buffer[2] = 0; 
        memcpy(&out_msg.buffer[3], &key[0], sizeof(key));
        // Send 
        if (RS485.sendMsgWithAck(&out_msg, RS485_REPEAT)) {
          p = goodkey; // play 
        }
        else {
          p = wrongkey; // play
        }
        iButtonRead = -1; // Temporarly disable scanning 
        memset(&key[0], 0x0, sizeof(key)); // Clear the key
      } else {
        iButtonRead = 0; // Enable scanning
      }
      readerMillis = millis();
    }    
  } // Tones 

  // Temperature readings
  if ((unsigned long)(millis() - tempMillis) > 300000) {
    tempMillis = millis();
    u.fval = (((float)analogRead(A6) * 0.003223)-0.5)*100;  
    out_msg.address = 0;
    out_msg.ctrl = FLAG_DTA;
    out_msg.data_length = 7;
    out_msg.buffer[0] = 'S'; // Sensor
    out_msg.buffer[1] = 'T'; // Temperature
    out_msg.buffer[2] = 0;   // local address
    out_msg.buffer[3] = u.b[0]; out_msg.buffer[4] = u.b[1];
    out_msg.buffer[5] = u.b[2]; out_msg.buffer[6] = u.b[3];   
    RS485.sendMsgWithAck(&out_msg, RS485_REPEAT);
  }

  // Send alive packet every 10 minutes
  if ((unsigned long)(millis() - aliveMillis) > 600000){
    aliveMillis = millis();
    out_msg.address = 0;
    out_msg.ctrl = FLAG_CMD;
    out_msg.data_length = 2; // PING = 2
    RS485.sendMsgWithAck(&out_msg, RS485_REPEAT);
  }

} // End main loop
