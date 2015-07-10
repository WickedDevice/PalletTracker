/*
  Vel Pendell
  
  Borrows from Kina Smith http://www.instructables.com/id/How-to-make-a-Mobile-Cellular-Location-Logger-with/?ALLSTEPS
  
*/

#define MAX_STRING_LENGTH      (32)
#define MAX_NUM_TARGET_MATCHES (5)

#include <Adafruit_GPS.h>
#include <SoftwareSerial.h>//We're not actually using this for anything, but the above library won't compile without it
#include <SD.h>

#define FONA_KEY 23 //powers board down
#define FONA_PS 22 //status pin. Is the board on or not?

char * APN = "wholesale"; //Set APN for Mobile Service

char response[64] = {0};
unsigned long time=0;
unsigned long interval=300000;
unsigned long ATtimeOut = 10000;
int keyTime = 2000; //Time needed to turn on the FONA
word count=0;

//Holders for Position Data
char GSM_Lat[64] = {0};
char GSM_Lon[64] = {0};
char GSM_Date[64] = {0};
char GSM_Time[64] = {0};

//some GPS variables
HardwareSerial gpsSerial = Serial2;
Adafruit_GPS GPS(&gpsSerial);
boolean usingInterrupt = false;

//File for SD card
File myFile;

// ========== helper methods ============== 

boolean addStringToList(char list[][MAX_STRING_LENGTH+1], char * str, uint8_t max_num_entries){
  uint16_t free_index = 0xFFFF;
  
  if(strlen(str) <= MAX_STRING_LENGTH){
    
    // search the list for an empty space
    // the last empty space must remain empty
    // so don't include it in the search
    for(uint16_t ii = 0; ii < (max_num_entries - 1); ii++){      
      uint16_t len = strlen(list[ii]);  
      if(0 == len){
        free_index = ii;
        break;
      }
    }
        
    // free index is the firs empty space in the list
    // or 0xFFFF if no free entries were found
    
    // if free index points to a viable position
    // then copy the candidate string into that position
    // and limit the number of characters copied
    if(free_index < (max_num_entries - 1)){         
      char * tgt_addr = list[free_index];
      memcpy(tgt_addr, 0, MAX_STRING_LENGTH+1); // fill with NULLs
      strncpy(tgt_addr, str, MAX_STRING_LENGTH);  // copy the string in      
      return true;
    }
    
  }
  
  return false;
}

// pass in an array of strings to match against
// the list is presumed to be terminated by a NULL string
// this function can only handle up to MAX_NUM_TARGET_MATCHES matching targets
boolean readStreamUntil(Stream * stream, char target_match[][MAX_STRING_LENGTH+1], uint8_t * match_idx, char * target_buffer, uint16_t target_buffer_length, int32_t timeout_ms){
  boolean match_found = false;
  uint32_t target_buffer_index = 0;
  static uint8_t match_char_idx[MAX_NUM_TARGET_MATCHES] = {0};
  unsigned long previousMillis = millis();
  
  memset(match_char_idx, 0, MAX_NUM_TARGET_MATCHES);
  
  while(!match_found){ // until a match is found
    unsigned long currentMillis = millis();
    if((timeout_ms > 0) && (currentMillis - previousMillis >= timeout_ms)){
       break;
    }
  
    if(stream->available()){
      previousMillis = millis(); // reset the timeout
      char chr = stream->read(); // read a character
      
      Serial.print(chr); // comment out only for debug
      
      // for each target match
      for(uint8_t ii = 0; ii < MAX_NUM_TARGET_MATCHES; ii++){
        uint16_t target_match_length = strlen(target_match[ii]);
        // an empty string in the list signals the end of the list
        if(target_match_length == 0){ 
          break;
        } 
        
        // if the current character is a match for this string
        // advance it's match index, 
        // otherwise reset its match index
        if(chr == target_match[ii][match_char_idx[ii]]){
           match_char_idx[ii]++;
        }
        else{
           match_char_idx[ii] = 0;
        }
        
        // if the match index is equal to the length of the string
        // then it's a complete match
        // return the string index that matched into match_idx
        // and return true to the caller
        if(match_char_idx[ii] >= target_match_length){      
          *match_idx = ii;
          match_found = true;
          break;
        }        
      }
      
      if(!match_found && target_buffer != NULL){
        if(target_buffer_index < target_buffer_length){
          target_buffer[target_buffer_index++] = chr;
        }
        else{
          break; // target buffer overflow
        }
      }      
    }
  }
  
  return match_found;   
}

// pass a single string to match against
// the string must not be longer than 31 characters
boolean readStreamUntil(Stream * stream, char * target_match, char * target_buffer, uint16_t target_buffer_length, int32_t timeout_ms){  
  char target_match_array[2][MAX_STRING_LENGTH + 1] = {0};
  uint8_t dummy_return;
  
  if(strlen(target_match) > 31){
    return false; 
  }
  else{
    addStringToList(target_match_array, target_match, 2);
    return readStreamUntil(stream, target_match_array, &dummy_return, target_buffer, target_buffer_length, timeout_ms);
  }
}

boolean readStreamUntil(Stream * stream, char * target_match, int32_t timeout_ms){
  return readStreamUntil(stream, target_match, NULL, 0, timeout_ms);
}

boolean readStreamUntil(Stream * stream, char * target_match){
  return readStreamUntil(stream, target_match, -1);
}

boolean readStreamUntil(Stream * stream, char target_match[][MAX_STRING_LENGTH+1], uint8_t * match_idx, int32_t timeout_ms){
  return readStreamUntil(stream, target_match, match_idx, NULL, 0, timeout_ms);
}

boolean readStreamUntil(Stream * stream, char target_match[][MAX_STRING_LENGTH+1], uint8_t * match_idx){
  return readStreamUntil(stream, target_match, match_idx, -1);  
}

void setup() {
  //setup the FONA miniGSM
  pinMode(FONA_PS, INPUT);
  pinMode(FONA_KEY,OUTPUT);
  digitalWrite(FONA_KEY, HIGH);
  
  //setup the GPSlogger
  GPS.begin(9600);
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time
    // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz
    // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_ANTENNA);
    // the nice thing about this code is you can have a timer0 interrupt go off
  // every 1 millisecond, and read data from the GPS for you. that makes the
  // loop code a heck of a lot easier!
  delay(1000);
  // Ask for firmware version
  gpsSerial.println(PMTK_Q_RELEASE);
  
  //Setup the serial ports
  Serial.begin(115200); //USB Serial
  Serial.println("Serial Ready");
  Serial2.begin(9600); //FONA miniGSM serial
  Serial.println("Serial2 Ready");
  Serial3.begin(9600); //GPSlogger serial
  Serial.println("Serial3 Ready");
  
  //Setup SD card
  Serial.print("Initializing SD card...");
  pinMode(53, OUTPUT);
  
  if(!SD.begin(53)) {
    Serial.println("initialization failed!");
    return;
  }
  Serial.println("initialization done.");   
}

void loop() {
  
  //remember the time
  time = millis();
  
  //Get GSM location data from FONA miniGSM
  turnOnFONA(); //turn on FONA
  delay(10000);
  setupGPRS(); //Setup a GPRS context
  if(getLocation()) {
        //Print Lat/Lon Values
        Serial.print(GSM_Lat);
        Serial.print(" : ");
        Serial.print(GSM_Lon);
        Serial.print(" at ");
        Serial.print(GSM_Time);
        Serial.print(" ");
        Serial.print(GSM_Date);
        Serial.println();
        turnOffFONA();
        flushFONA();
  }     
  
  //Get GPS location data from GPSlogger
    // if a sentence is received, we can check the checksum, parse it...
  if (GPS.newNMEAreceived()) {
    // a tricky thing here is if we print the NMEA sentence, or data
    // we end up not listening and catching other sentences! 
    // so be very wary if using OUTPUT_ALLDATA and trytng to print out data
    //Serial.println(GPS.lastNMEA());   // this also sets the newNMEAreceived() flag to false
  
    if (!GPS.parse(GPS.lastNMEA()))   // this also sets the newNMEAreceived() flag to false
      return;  // we can fail to parse a sentence in which case we should just wait for another
  }
  
  
  //Record location data to SD card
  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  myFile = SD.open("PositionProtoype.txt", FILE_WRITE);
  
  // if the file opened okay, write to it:
  if (myFile) {
    Serial.print("Writing to PositionPrototype.txt...");
    
    myFile.print("count: "); myFile.println(count);
    myFile.print("Milliseconds: "); myFile.println(time);
    
    myFile.println("GSM data");
    myFile.print("Latitude: "); myFile.println(GSM_Lat);
    myFile.print("Longitude: "); myFile.println(GSM_Lon);
    myFile.print("Date: "); myFile.println(GSM_Date);
    myFile.print("Time: "); myFile.println(GSM_Time);
    
    myFile.println("GPS data:");
    myFile.print("\nTime: ");
    myFile.print(GPS.hour, DEC); myFile.print(':');
    myFile.print(GPS.minute, DEC); myFile.print(':');
    myFile.print(GPS.seconds, DEC); myFile.print('.');
    myFile.println(GPS.milliseconds);
    myFile.print("Date: ");
    myFile.print(GPS.day, DEC); myFile.print('/');
    myFile.print(GPS.month, DEC); myFile.print("/20");
    myFile.println(GPS.year, DEC);
    myFile.print("Fix: "); myFile.print((int)GPS.fix);
    myFile.print(" quality: "); myFile.println((int)GPS.fixquality); 
    if (GPS.fix) {
      myFile.print("Location: ");
      myFile.print(GPS.latitude, 4); myFile.print(GPS.lat);
      myFile.print(", "); 
      myFile.print(GPS.longitude, 4); myFile.println(GPS.lon);
      myFile.print("Location (in degrees, works with Google Maps): ");
      myFile.print(GPS.latitudeDegrees, 4);
      myFile.print(", "); 
      myFile.println(GPS.longitudeDegrees, 4);
      
      myFile.print("Speed (knots): "); myFile.println(GPS.speed);
      myFile.print("Angle: "); myFile.println(GPS.angle);
      myFile.print("Altitude: "); myFile.println(GPS.altitude);
      myFile.print("Satellites: "); myFile.println((int)GPS.satellites);
    }
	// close the file:
    myFile.close();
    Serial.println("done.");
  } else {
    // if the file didn't open, print an error:
    Serial.println("error opening test.txt");
  }
  
  //update count & delay
  count++;
  while(millis() - time < interval){};
}

boolean getLocation() {
    char content[64] = {0};
    uint8_t match_index = 0;
    const uint8_t num_targets = 4;
    char target_match[num_targets][MAX_STRING_LENGTH+1] = {0};
    addStringToList(target_match, "OK", num_targets);  // match 0
    addStringToList(target_match, "ERROR", num_targets); // match 1
    addStringToList(target_match, "+CIPGSMLOC:", num_targets); // match 2
    if(readStreamUntil(&Serial2, target_match, &match_index, ATtimeOut)){
      if(match_index == 2){
        if(readStreamUntil(&Serial2, target_match, &match_index, content, 63, ATtimeOut)){
          if(match_index == 0){
            Serial.print("Got content: \"");
            Serial.print(content);
            Serial.println("\"");
            
            char * token = strtok(content, ",");
            int field_number = 0;
            while(token != NULL){
               Serial.print("Token #");
               Serial.print(field_number);
               Serial.print(": ");
               Serial.println(token);
               
               switch(field_number){
                 case 0: // location code - discard
                    break;
                 case 1: // longitude
                    strncpy(GSM_Lon, token, 63);
                    break;
                 case 2: // latitude
                    strncpy(GSM_Lat, token, 63);
                    break;
                 case 3: // date
                    strncpy(GSM_Date, token, 63);
                    break;
                 case 4: // time
                    strncpy(GSM_Time, token, 63);     
                    break;
               }
               
               field_number++;
               token = strtok(NULL, ",");
            }
            Serial.println("Done.");
            
            if(field_number != 5){
              Serial.print("Panic 3, field number = ");
              Serial.println(field_number); 
            }
            
          }
          else{
            Serial.print("Panic 2, match index = ");
            Serial.println(match_index);
          }  
        }
        else{
          Serial.println("Timeout waiting for 'OK' after '+CIPGSMLOC:'"); 
        }
      }
      else{
        // PANIC
        Serial.println("Panic: 1"); 
      }
    }
    
    Serial2.println("AT+CIPGSMLOC=1,1");
    
    /*    
    char character;
    int complete = 0;
    char c;
    unsigned long commandClock = millis();                      // Start the timeout clock

    while(!complete && commandClock <= millis()+ATtimeOut) { // Need to give the modem time to complete command
        while(!Serial2.available() && commandClock <= millis()+ATtimeOut); //wait while there is no data
        while(Serial2.available()) {   // if there is data to read...
            c = Serial2.read();
            if(c == 0x0A || c == 0x0D) {
            } else {
                content.concat(c);
            }
        }
        if(content.startsWith("+CIPGSMLOC: 0,")) {
            Serial.println("Got Location"); //+CIPGSMLOC: 0,-73.974037,40.646976,2015/02/16,21:05:11OK
            GSM_Lon = content.substring(14, 24);
            GSM_Lat = content.substring(25, 34);
            GSM_Date = content.substring(35, 45);
            GSM_Time = content.substring(46,54);
            return 1;
        } else {
            Serial.print("ERROR: ");
            Serial.println(content);
            return 0;
        }
        complete = 1; //this doesn't work. 
    }
  */
}
void setupGPRS() { //all the commands to setup a GPRS context and get ready for HTTP command
    //the sendATCommand sends the command to the FONA and waits until the recieves a response before continueing on. 
    Serial.print("Disable echo: ");
    if(sendATCommand("ATE0")) { //disable local echo
        Serial.println(response);
    }
    Serial.print("Set to TEXT Mode: ");
    if(sendATCommand("AT+CMGF=1")){ //sets SMS mode to TEXT mode....This MIGHT not be needed. But it doesn't break anything with it there. 
        Serial.println("...OK");
        Serial.print("Response - '");
        Serial.print(response);
        Serial.println("'");
    }
    else{
        Serial.println("...Failed");
    }
    Serial.print("Attach GPRS: ");
    if(sendATCommand("AT+CGATT=1")){ //Attach to GPRS service (1 - attach, 0 - disengage)
        Serial.println("...OK");
        Serial.print("Response - '");
        Serial.print(response);
        Serial.println("'");
    }
    else{
        Serial.println("...Failed");
    }
    Serial.print("Set Connection Type To GPRS: "); //AT+SAPBR - Bearer settings for applications based on IP
    if(sendATCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"")){ //3 - Set bearer perameters
        Serial.println("...OK");
        Serial.print("Response - '");
        Serial.print(response);
        Serial.println("'");
    }
    else{
        Serial.println("...Failed");
    }
    Serial.print("Set APN: ");
    if(setAPN()) {
        Serial.println("...OK");
        Serial.print("Response - '");
        Serial.print(response);
        Serial.println("'");
    }
    else{
        Serial.println("...Failed");
    }
    Serial.print("Set SAPBR: ");
    if(sendATCommand("AT+SAPBR=1,1")) { //Open Bearer
        if(strncmp(response, "OK", 2) == 0) {
            Serial.println("Engaged GPRS");
        } else {
            Serial.println("GPRS Already on");
        }
    }
    else{
       Serial.println("...Failed");
    }
}

boolean sendATCommand(char Command[]) { //Send an AT command and wait for a response
    static char content[64] = {0};
    
    uint8_t match_index = 0;
    const uint8_t num_targets = 3;
    static char target_match[num_targets][MAX_STRING_LENGTH+1] = {0};
    addStringToList(target_match, "OK", num_targets);  // match 0
    addStringToList(target_match, "ERROR", num_targets); // match 1    
    
    Serial2.println(Command);
    
    Serial.print("Sending command: '");
    Serial.print(Command);
    Serial.println("'");
    
    if(readStreamUntil(&Serial2, target_match, content, 63, ATtimeOut)){
      if(match_index == 0){
        strncpy(response, content, 63);
        return true;
      }
      else{
        Serial.print("Error response to '");
        Serial.print(Command);
        Serial.println("'");
      }
    }
    else{
      Serial.print("Timeout waitng for 'OK' in response to '");
      Serial.print(Command);
      Serial.println("'");
      return false;
    }

    /*
    int complete = 0; // have we collected the whole response?
    char c; //capture serial stream
    String content; //place to save serial stream
    unsigned long commandClock = millis(); //timeout Clock
    Serial2.println(Command); //Print Command
    while(!complete && commandClock <= millis() + ATtimeOut) { //wait until the command is complete
        while(!Serial2.available() && commandClock <= millis()+ATtimeOut); //wait until the Serial Port is opened
        while(Serial2.available()) { //Collect the response
            c = Serial2.read(); //capture it
            if(c == 0x0A || c == 0x0D); //disregard all new lines and carrige returns (makes the String matching eaiser to do)
            else content.concat(c); //concatonate the stream into a String
        }
        //Serial.println(content); //Debug
        response = content; //Save it out to a global Variable (How do you return a String from a Function?)
        complete = 1;  //Lable as Done.
    }
    if (complete ==1) return 1; //Is it done? return a 1
    else return 0; //otherwise don't (this will trigger if the command times out) 
    
    */   
    
    /*
        Note: This function may not work perfectly...but it works pretty well. I'm not totally sure how well the timeout function works. It'll be worth testing. 
        Another bug is that if you send a command that returns with two responses, an OK, and then something else, it will ignore the something else and just say DONE as soon as the first response happens. 
        For example, HTTPACTION=0, returns with an OK when it's intiialized, then a second response when the action is complete. OR HTTPREAD does the same. That is poorly handled here, hence all the delays up above. 
    */
}

boolean setAPN() { //Set the APN. See sendATCommand for full comments on flow
    static char cmd[64] = {0};
    snprintf(cmd, 63, "", APN);
    
    /*
    int complete = 0;
    char c;
    String content;
    unsigned long commandClock = millis();                      // Start the timeout clock
    Serial2.print("AT+SAPBR=3,1,\"APN\",\"");
    Serial2.print(APN);
    Serial2.print("\"");
    Serial2.println();
    while(!complete && commandClock <= millis() + ATtimeOut) {
        while(!Serial2.available() && commandClock <= millis()+ATtimeOut);
        while(Serial2.available()) {
            c = Serial2.read();
            if(c == 0x0A || c == 0x0D);
            else content.concat(c);
        }
        response = content;
        complete = 1; 
    }
    if (complete ==1) return 1;
    else return 0;
    */
}

void flushFONA() { //if there is anything is the Serial2 serial Buffer, clear it out and print it in the Serial Monitor.
    char inChar;
    while (Serial2.available()){
        inChar = Serial2.read();
        Serial.write(inChar);
        delay(20);
    }
}

void turnOnFONA() { //turns FONA ON
    if(! digitalRead(FONA_PS)) { //Check if it's On already. LOW is off, HIGH is ON.
        Serial.print("FONA was OFF, Powering ON: ");
        digitalWrite(FONA_KEY,LOW); //pull down power set pin
        unsigned long KeyPress = millis(); 
        while(KeyPress + keyTime >= millis()) {} //wait two seconds
        digitalWrite(FONA_KEY,HIGH); //pull it back up again
        Serial.println("FONA Powered Up");
    } else {
        Serial.println("FONA Already On, Did Nothing");
    }
}

void turnOffFONA() { //does the opposite of turning the FONA ON (ie. OFF)
    if(digitalRead(FONA_PS)) { //check if FONA is OFF
        Serial.print("FONA was ON, Powering OFF: "); 
        digitalWrite(FONA_KEY,LOW);
        unsigned long KeyPress = millis();
        while(KeyPress + keyTime >= millis()) {}
        digitalWrite(FONA_KEY,HIGH);
        Serial.println("FONA is Powered Down");
    } else {
        Serial.println("FONA is already off, did nothing.");
    }
}


