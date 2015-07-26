/*
* Arduino sensor data client
* 
* Reads sensors and sends data to net as HTTP POST request in JSON format. To make use of this code, 
* modify CONFIGURATION and SENSOR CONFIGURATION blocks
* 
* Requirements: 
* - Arduino Mega 2560, CC3000 Wifi shield
*    - http://www.miniinthebox.com/funduino-mega-2560-r3-development-board_p903322.html
*    - http://www.miniinthebox.com/cc3000-wifi-shield-for-arduino-r3-with-sd-card-support-mega2560_p3254551.html
* - Arduino development environment 1.0.6 (Adafruit_CC3000 library does not work properly with 1.5.x or 1.6.x)
* - Adafruit CC3000 library https://learn.adafruit.com/adafruit-cc3000-wifi/cc3000-library-software
*
* Sensor support is implemented for defined sensor setups. Currently implemented sensor setups include:
* - Environment (3 analog pins): 
*   - startPin:   LM35 Linear temperature sensor (http://www.miniinthebox.com/lm35-linear-temperature-sensor-module-black_p991017.html )
*   - startPin+1: KY-018 Photo resistor module (https://tkkrlab.nl/wiki/Arduino_37_sensors )
*   - startPin+2: TODO: humidity probe (http://www.miniinthebox.com/amt1001-resistive-humidity-module-humidity-sensor-humidity-probe_p628098.html )
* - Soil (1 analog pin):
*    - pin:       Soil humidity sensor (http://www.miniinthebox.com/frearduino-soil-humidity-sensor-for-arduino-works-with-official-arduino-boards_p706865.html )
* - Temp (1 analog pin), with alert feature:
*    - pin:       LM35 Linear temperature sensor (http://www.miniinthebox.com/lm35-linear-temperature-sensor-module-black_p991017.html )
*/
#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>
#include <avr/wdt.h>

/////////////////////////////////////////////////////////////////////////////////////////
// CONFIGURATION

// Defines whether extendive debug messages are printed to serial or not
#define DEBUG_CODE         true

//Email address to send alterts to
#define ALERT_EMAIL        "pesavola@gmail.com"

// Unique id of this client. Must be numeric.
#define CLIENT_ID          400

// Name of wlan to use, case snsitive
#define WLAN_SSID          "Savo"           // cannot be longer than 32 characters!

// Passwor of wlan to use, case snsitive
#define WLAN_PASS          "savonsydan"

// Security of wifi - can be WLAN_SEC_UNSEC, WLAN_SEC_WEP, WLAN_SEC_WPA or WLAN_SEC_WPA2
#define WLAN_SECURITY      WLAN_SEC_WPA2

/////////////////////////////////////////////////////////////////////////////////////////
// ADAFRUIT CC

// These are the interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3  // MUST be an interrupt pin!

// These can be any two pins
#define ADAFRUIT_CC3000_VBAT  5
#define ADAFRUIT_CC3000_CS    10

// Use hardware SPI for the remaining pins
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(ADAFRUIT_CC3000_CS, 
                                         ADAFRUIT_CC3000_IRQ, 
                                         ADAFRUIT_CC3000_VBAT,
                                         SPI_CLOCK_DIVIDER); // you can change this clock speed

// Amount of time to wait (in milliseconds) with no data received before closing the connection
#define IDLE_TIMEOUT_MS  3000      

/////////////////////////////////////////////////////////////////////////////////////////
// CLIENT SOFTWARE

// Client software version number
#define CLIENT_VERSION     "0.41"

// Web server to call
#define WEBSITE      "nrtestps.mybluemix.net"
#define WEBPAGE      "/ct/update"

// Internal settings
#define NAME_LENGTH        31
#define ALERT_LENGTH       31 
#define NUMBER_LENGTH       9
#define DECIMALS            2

// server call interval: 30 min
# define DATA_SEND_INTERVAL 1800000

// For testing: server call interval: 10 sec
//# define DATA_SEND_INTERVAL 10000

// measurements to take mean over
#define MEASUREMENTS 9

typedef struct {
  float min;
  float max;
  byte condition;
} minmaxalert;

// Environment sensor setting group
typedef struct {
  char name[NAME_LENGTH+1];
  byte startPin;
  float humidity;
  float temperature;
  float illuminance;
} environment;

// Soil sensor setting group
typedef struct {
  char name[NAME_LENGTH+1];
  byte pin;
  float humidity;
} soil;

// Temperature sensor setting group
typedef struct {
  char name[NAME_LENGTH+1];
  byte pin;
  float temp;
  minmaxalert alert;
} temp;

#define CONDITION_OK               0
#define TEMP_CONDITION_TOO_WARM    1
#define TEMP_CONDITION_TOO_COLD    2

/////////////////////////////////////////////////////////////////////////////////////////
// SENSOR CONFIGURATION

// Environment sensor settings. Each require 3 analog pins
// Environment name, first analog pin to use (also requires the next 2), value initializations
#define ENVIRONMENT_COUNT    1                         // Must match to row below!
environment environments[] = {{"in",0,0,0,0}}; 

// Soil sensor settings. Each require 1 analog pin
// Soil name, analog pin to use, value initialization
#define SOIL_COUNT           1                         // Must match to row below!!!
soil soils[] =               {{"soil_1",4,0}};   

// Temperature sensor settings. Each require 1 analog pin
// Sensor name, analog pin to use, value initialization, alert min limit, alert max limit, initial condition
#define TEMP_COUNT           0                         // Must match to row below!!!
temp temps[] =               {};  

/////////////////////////////////////////////////////////////////////////////////////////
// CODE: global variables

uint32_t ip;
Adafruit_CC3000_Client connection;
char buf[32];
int measurements[MEASUREMENTS];

////////////////////////////////////////////////////////////
// CODE: setup

// Sets up the system. Called at program startup.
void setup(void)
{
  // disable watchdog at startup
  wdt_disable();
  
  Serial.begin(9600);
  Serial.print("Starting up Arduino sensor data client v");
  Serial.print(CLIENT_VERSION);
  Serial.print(" for client ");
  Serial.println(CLIENT_ID); 
  
  Serial.print("\nInitializing cc3000...");
  if (!cc3000.begin()) {
    Serial.println(" Couldn't begin! Check wiring.");
    while(1);
  }
  Serial.println(" done");
  
  // TODO: we could make some safety checks here
  // sensors and their counts should match
  // Greehouseid and its length should match
  
  Serial.println("Client started");
}

////////////////////////////////////////////////////////////
// CODE: networking

// Creates network connection by using cc3000 wifi shield
boolean connectToNetwork(void) {
  Serial.print("\nAttempting to connect to WiFi "); 
  Serial.print(WLAN_SSID);
  Serial.print("...");
  wdt_disable(); // onnecting might take too long time
  if (!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) {
    Serial.println(" failed");
    return false;
  } 
  else {
    Serial.println(" done");
  }
  wdt_enable(WDTO_8S);
  wdt_reset();
  
  Serial.print("Request DHCP... ");
  while (!cc3000.checkDHCP()) {
    delay(1000); 
  }
  Serial.println(" done");
  wdt_reset();

  Serial.print("Connection details:");
  while (!displayConnectionDetails()) {
    delay(1000);
  }
  wdt_reset();

  ip = 0;
  Serial.print("Resolve server ip: ");
  Serial.print(WEBSITE); 
  Serial.print("... ");
  while (ip == 0) {
    if (!cc3000.getHostByName(WEBSITE, &ip)) {
      Serial.println(" Couldn't resolve!");
    }
    delay(500);
  }
  cc3000.printIPdotsRev(ip);
  Serial.println(""); 
  wdt_reset();
  
  delay(500);
  
  // Connect to server
  connection = cc3000.connectTCP(ip, 80);
  wdt_reset();
  return connection.connected();
}  

// Disconnects network connection
void disconnectFromNetwork(void) {
  Serial.print("Disconnecting...");

  wdt_reset();
  connection.close();
  connection = 0;
 
  wdt_reset();
  cc3000.disconnect();

  Serial.println(" done");
}

////////////////////////////////////////////////////////////
// CODE: measuring

// Reads analog pin for MEASUREMENTS times and takes a mean on values. Returned value is between 0 and 1023
float readAnalogWithMean(int pin) {
  for(int i = 0; i < MEASUREMENTS; i++) {
    measurements[i] = analogRead(pin);
  }
  
  return mean(measurements, MEASUREMENTS);
}

// Measures environment data
void measureEnvironments(void) {
  for(int i = 0; i < ENVIRONMENT_COUNT; i++ ) { 

    // temp
    environments[i].temperature = readAnalogWithMean(environments[i].startPin)*5*100/1023;  
       
    // illuminance
    float value = 1023 - readAnalogWithMean(environments[i].startPin+1); 
    if(value > 1000) value = 1000;
    environments[i].illuminance = value;
    
    // humidity
    // TODO
    environments[i].humidity = 0;
  }
}

// Measures soil data
void measureSoils(void) {
  for(int i = 0; i < SOIL_COUNT; i++ ) {
    
    // humidity
    soils[i].humidity = readAnalogWithMean(soils[i].pin)*100/1023;
  }
}

// Measures temp data
void measureTemps(void) {
  for(int i = 0; i < TEMP_COUNT; i++ ) {
    
    // temp
    temps[i].temp = readAnalogWithMean(temps[i].pin)*5*100/1023; 
   
    // create alerts if necessary
    if(temps[i].temp > temps[i].alert.max) temps[i].alert.condition = TEMP_CONDITION_TOO_WARM; 
    else if(temps[i].temp < temps[i].alert.min) temps[i].alert.condition = TEMP_CONDITION_TOO_COLD;
    else temps[i].alert.condition = CONDITION_OK;
  }
}

// Makes measurements from sensors
void measure(void) {
  if(ENVIRONMENT_COUNT != 0) {
    Serial.print("Measuring environments...");
    measureEnvironments();
    Serial.println(" done");
  }
  
  if(SOIL_COUNT != 0) {
    Serial.print("Measuring soils...");
    measureSoils();
    Serial.println(" done");
  }
  
  if(TEMP_COUNT != 0) {
    Serial.print("Measuring temps...");
    measureTemps();
    Serial.println(" done");
  }
}

////////////////////////////////////////////////////////////
// CODE: server content

// Counts basic JSON content length. Must match createBasicContent
int getBasicContentLength(void) {
  return getNumericContentLength("thing", CLIENT_ID, 0, true);
}

// Writes basic JSON content. Must match getBasicContentLength
void createBasicContent(void) {
  createNumericContent("thing", CLIENT_ID, 0, true);
}

// Counts environment JSON content length. Must match createEnvironmentContent
int getEnvironmentContentLength(int idx) {
  environment e = environments[idx];

  return getStringContentLength("name", e.name, FALSE) +
         getNumericContentLength("temp", e.temperature, DECIMALS, FALSE) +
         getNumericContentLength("humidity", e.humidity, DECIMALS, FALSE) +
         getNumericContentLength("luminence", e.illuminance, DECIMALS, TRUE);
}

// Writes environment JSON content. Must match getEnvironmentContentLength
void createEnvironmentContent(int idx) {
  environment e = environments[idx];
  
  createStringContent("name", e.name, FALSE);
  createNumericContent("temp", e.temperature, DECIMALS, FALSE);
  createNumericContent("humidity", e.humidity, DECIMALS, FALSE);
  createNumericContent("luminence", e.illuminance, DECIMALS, TRUE);
}

// Counts environments JSON content length. Must match createEnvironmentsContent
int getEnvironmentsContentLength(void) {
  int length = 17;
  
  for( int i = 0; i < ENVIRONMENT_COUNT; i++ ) {
      length += 1;
      length += getEnvironmentContentLength(i);
      
      if( i == (ENVIRONMENT_COUNT-1)) {
        length += 1;
      }
      else {
        length += 2;
      }
  }
    
  length += 1;
  
  return length;
}

// Writes environments JSON content. Must match getEnvironmentsContentLength
void createEnvironmentsContent(void) {   
  conPrint("\"environments\": [");   // length: 17
  
  for( int i = 0; i < ENVIRONMENT_COUNT; i++ ) {
      conPrint("{");                 // length: 1
      createEnvironmentContent(i);                // length: getEnvironmentContentLength(i)
      
      if( i == (ENVIRONMENT_COUNT-1)) {
        conPrint("}");               // length: 1
      }
      else {
        conPrint("},");              // length: 2
      }
  }
    
  conPrint("]");                     // length: 1
}

// Counts soil JSON content length. Must match createSoilContent
int getSoilContentLength(int idx) {
  soil s = soils[idx];
  
  return getStringContentLength("name", s.name, FALSE) +
         getNumericContentLength("humidity", s.humidity, DECIMALS, TRUE);
}

// Writes soil JSON content. Must match getSoilContentLength
void createSoilContent(int idx) {
  soil s = soils[idx];
  
  createStringContent("name", s.name, FALSE);
  createNumericContent("humidity", s.humidity, DECIMALS, TRUE);
}

// Counts soils JSON content length. Must match createSoilsContent
int getSoilsContentLength(void) {  
    int length = 10;
    
    for( int i = 0; i < SOIL_COUNT; i++ ) {
      length += 1;
      length += getSoilContentLength(i);
      
      if( i == (SOIL_COUNT-1)) {
        length += 1;
      }
      else {
        length += 2;
      }
    }
    length += 1;
    
    return length;
}

// Writes soils JSON content. Must match getSoilsContentLength
void createSoilsContent(void) {
    conPrint("\"soils\": [");        // length: 10
    
    for( int i = 0; i < SOIL_COUNT; i++ ) {
      conPrint("{");                 // length: 1
      createSoilContent(i);                       // length: getSoilContentLength(i)
      
      if( i == (SOIL_COUNT-1)) {
        conPrint("}");               // length: 1
      }
      else {
        conPrint("},");              // length: 2
      }
    }
    conPrint("]");                   // length: 2
}

// Counts temp JSON content length. Must match createTempContent
int getTempContentLength(int idx) {
  temp t = temps[idx];
  
  return getStringContentLength("name", t.name, FALSE) +
         getNumericContentLength("temp", t.temp, DECIMALS, TRUE);
}

// Writes temp JSON content. Must match getTempContentLength
void createTempContent(int idx) {
  temp t = temps[idx];
  
  createStringContent("name", t.name, FALSE);
  createNumericContent("temp", t.temp, DECIMALS, TRUE);
}

// Counts temps JSON content length. Must match createTempsContent
int getTempsContentLength(void) {  
    int length = 10;
    
    for( int i = 0; i < TEMP_COUNT; i++ ) {
      length += 1;
      length += getTempContentLength(i);
      
      if( i == (TEMP_COUNT-1)) {
        length += 1;
      }
      else {
        length += 2;
      }
    }
    length += 1;
    
    return length;
}

// Writes temps JSON content. Must match getTempsContentLength
void createTempsContent(void) {
    conPrint("\"temps\": [");        // length: 10
    
    for( int i = 0; i < TEMP_COUNT; i++ ) {
      conPrint("{");                 // length: 1
      createTempContent(i);                       // length: getTempContentLength(i)
      
      if( i == (TEMP_COUNT-1)) {
        conPrint("}");               // length: 1
      }
      else {
        conPrint("},");              // length: 2
      }
    }
    conPrint("]");                   // length: 1
}

// loops through sensor groups supporting alerts and checks if some has alerts
boolean isAlerts() {
  
   // temps
   if(TEMP_COUNT > 0 ) {
      for( int i = 0; i < TEMP_COUNT; i++ ) {
         if( temps[i].alert.condition != CONDITION_OK) return true;
      }
   }
      
   return false;
}

// Counts temp alerts JSON content length. Must match createTempAlertsContent
int getTempAlertsLength(boolean first) {
  int length = 0;
  
  for( int i = 0; i < TEMP_COUNT; i++ ) {
    if( temps[i].alert.condition != CONDITION_OK) {
      if(first) length += 1;
      else length += 2;
      first = false;
      
      length += getStringContentLength("sensor", temps[i].name, FALSE);
      length += getNumericContentLength("value", temps[i].temp, DECIMALS, FALSE);
      
      if( temps[i].alert.condition == TEMP_CONDITION_TOO_WARM ) length += getStringContentLength("txt", "too warm", TRUE);
      else if( temps[i].alert.condition == TEMP_CONDITION_TOO_COLD ) length += getStringContentLength("txt", "too cold", TRUE);
      else length += getStringContentLength("txt", "unknown", TRUE);
      
      length += 1;
    }
  }
  
  return length;
}

// Counts temp alerts JSON content length. Must match createTempAlertsContent
// Returns updated status for paprameter first
boolean createTempAlerts(boolean first) {
  for( int i = 0; i < TEMP_COUNT; i++ ) {
    if( temps[i].alert.condition != CONDITION_OK) {
        if(first) conPrint("{");                   // length: 1
        else conPrint(",{");                       // length: 2
        first = false;
        
        createStringContent("sensor", temps[i].name, FALSE);
        createNumericContent("value", temps[i].temp, DECIMALS, FALSE);
        
        if( temps[i].alert.condition == TEMP_CONDITION_TOO_WARM ) createStringContent("txt", "too warm", TRUE);
        else if( temps[i].alert.condition == TEMP_CONDITION_TOO_COLD ) createStringContent("txt", "too cold", TRUE);
        else createStringContent("txt", "unknown", TRUE);
        
        conPrint("}");                   // length: 1
    }
  }
  
  return first;
}

// Counts alerts JSON content length. Must match createAlertsContent
int getAlertsContentLength(void) {    
    if( isAlerts() ) {
      int length = 0;
      length += 12;
      length += getStringContentLength("to", ALERT_EMAIL, FALSE);
      length += 13;     
      int startLength = length;
      
      if(TEMP_COUNT > 0 ) {
        length += getTempAlertsLength(length == startLength);
      }
      
      length += 1;    
      length += 1;
      
      return length;
    }
    else {
      return 0;
    }
}

// Writes alerts JSON content. Must match getAlertsContentLength
void createAlertsContent(void) {
  if( isAlerts() ) {
     conPrint(",\"alerts\": {");       // length: 12
     createStringContent("to", ALERT_EMAIL, FALSE);
     conPrint("\"messages\": [");     // length: 13
     boolean first = true;
    
     if(TEMP_COUNT > 0 ) {
        first = createTempAlerts(first);
     }
    
     conPrint("]");                       // length: 1  
     conPrint("}");                       // length: 1
  }
  else {
    return;
  }  
}

// Counts JSON content length. Must match createContent
int getContentLength(void) {
  return 1 + 
         getBasicContentLength() + 
         ((ENVIRONMENT_COUNT != 0)?(1 + getEnvironmentsContentLength()):0) + 
         ((SOIL_COUNT != 0)?(1 + getSoilsContentLength()):0) + 
         ((TEMP_COUNT != 0)?(1 + getTempsContentLength()):0) + 
         getAlertsContentLength() +
         1;
}

// Writes JSON content. Must match getContentLength
void createContent(void) {  
    
    conPrint("{");                                // length: 1
    wdt_reset();
    
    createBasicContent();                         // length: getBasicContentLength()
    wdt_reset();
    
    if(ENVIRONMENT_COUNT != 0) {
      conPrint(",");                              // length: 1
      createEnvironmentsContent();                // length: getEnvironmentsContentLength()
      wdt_reset();
    }
    
    if(SOIL_COUNT != 0) {
      conPrint(",");                              // length: 1
      createSoilsContent();                       // length: getSoilsContentLength()
      wdt_reset();
    }
    
    if(TEMP_COUNT != 0) {
      conPrint(",");                              // length: 1
      createTempsContent();                       // length: getTempsContentLength()
      wdt_reset();
    }
    
    createAlertsContent();                         // length: getAlertsContentLength()
    wdt_reset();
    
    conPrint("}");                                 // length: 1
    wdt_reset();
}

// Sends HTTP POST with JSON data to server
void callServer(void) {
    Serial.println("Sending request to server");
    
    // request header
    wdt_reset();
    Serial.println("header");
    int contentLength =  getContentLength();
    
    conPrint("POST ");
    conPrint(WEBPAGE);
    conPrint(" HTTP/1.1\r\n");
    conPrint("Host: "); conPrint(WEBSITE); conPrint("\r\n");
    conPrint("Content-Type: application/json"); conPrint("\r\n");
    conPrint("User-Agent: Arduino/1.0"); conPrint("\r\n");
    conPrint("Connection: close"); conPrint("\r\n");
    itoa(contentLength, buf, 10);
    conPrint("Content-Length: "); conPrint(buf); conPrint("\r\n");
    conPrintln("");
    Serial.println("header ok");
    
    // request content
    wdt_reset();
    Serial.println("content");
    createContent();
    conPrintln("");
    connection.flush();
    Serial.println("content ok");
    
    Serial.println("Server reply:");
    wdt_reset();
    
    // Read data until either the connection is closed, or the idle timeout is reached.
    unsigned long lastRead = millis();
    while (connection.connected() && (millis() - lastRead < IDLE_TIMEOUT_MS)) {
      while (connection.available()) {
        char c = connection.read();
        Serial.print(c);
        lastRead = millis();
      }
    }
    
    Serial.println("Server reply ends here");
}

////////////////////////////////////////////////////////////
// CODE: main loop

// Loop, called automatically in a loop after setup has been executed
void loop(void)
{
  // enable watchdog
  wdt_enable(WDTO_8S);
  wdt_reset();
  
  // Try to connect to network  
  if( connectToNetwork() ) {
    // Connected to network
    wdt_reset();
    measure();
    
    wdt_reset();
    callServer();
   
    wdt_reset(); 
    disconnectFromNetwork();
  }
  else {
    // Failed to connect to network
    Serial.println("Failed to send data to server: connection failed");
  } 
  
  // Sleep until next measurement time
  Serial.println("Taking a nap until next measuring time");
  wdt_reset();
  wdt_disable();
  delay(DATA_SEND_INTERVAL); 
}

////////////////////////////////////////////////////////////
// CODE: sub routines

// Calculates mean over values
int mean(int a[], int size) {
   sort(a, size);
   return a[(size+1)/2];
}

// Sorts an array of values
void sort(int a[], int size) {
    for(int i=0; i<(size-1); i++) {
        for(int o=0; o<(size-(i+1)); o++) {
                if(a[o] > a[o+1]) {
                    int t = a[o];
                    a[o] = a[o+1];
                    a[o+1] = t;
                }
        }
    }
}

// Counts length of a string name value pair in JSON structure
int getStringContentLength(char name[], char value[], boolean isLast) {
  return 1 + strlen(name) + 3 + strlen(value) + 1 + (isLast?0:1);
}

// Writes a string name value pair in JSON structure
void createStringContent(char name[], char value[], boolean isLast) {
   conPrint("\"");     // length: 1
   conPrint(name);     // length: strlen(name)
   conPrint("\":\"");  // length: 3
   conPrint(value);    // length: strlen(value)
   conPrint("\"");     // length: 1
   
   if(!isLast ) conPrint(",");     // length: 1
}

// Counts length of a numeric name value pair in JSON structure. Must match 
int getNumericContentLength(char name[], float value, int decimals, boolean isLast) {
  return 1 + strlen(name) + 2 + NUMBER_LENGTH + (isLast?0:1);
}

// Writes a numeric name value pair in JSON structure
void createNumericContent(char name[], float value, int decimals, boolean isLast) {
   if(decimals >= NUMBER_LENGTH) Serial.println("createNumericContent: Unexpected arguments. Decimals should be smaller than NUMBER_LENGTH");
  
   conPrint("\"");     // length: 1
   conPrint(name);     // length: strlen(name)
   conPrint("\":");    // length: 2
   
   dtostrf(value, NUMBER_LENGTH, decimals, buf);  
   conPrint(buf);      // length: NUMBER_LENGTH
   
   if(!isLast) conPrint(",");     // length: 1
}

void conPrint(char txt[]) {
  if(DEBUG_CODE) Serial.print(txt);
  connection.fastrprint(txt);
}

void conPrintln(char txt[]) {
  if(DEBUG_CODE) Serial.println(txt);
  connection.fastrprintln(txt);
}

// Prints connection details to Serial
bool displayConnectionDetails(void)
{
  uint32_t ipAddress, netmask, gateway, dhcpserv, dnsserv;
  
  if(!cc3000.getIPAddress(&ipAddress, &netmask, &gateway, &dhcpserv, &dnsserv))
  {
    Serial.println("Unable to retrieve the IP Address!\r\n");
    return false;
  }
  else
  {
    Serial.print("\nIP Addr: "); cc3000.printIPdotsRev(ipAddress);
    Serial.print("\nNetmask: "); cc3000.printIPdotsRev(netmask);
    Serial.print("\nGateway: "); cc3000.printIPdotsRev(gateway);
    Serial.print("\nDHCPsrv: "); cc3000.printIPdotsRev(dhcpserv);
    Serial.print("\nDNSserv: "); cc3000.printIPdotsRev(dnsserv);
    Serial.println();
    return true;
  }
}
