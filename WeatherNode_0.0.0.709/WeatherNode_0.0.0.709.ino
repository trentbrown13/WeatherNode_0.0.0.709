  
/************************************************************************************
  Ver 000709

  Implements HTTP Update and subsribes to nodered looking for update. If Update == 1, then
  calls checkForUpdate.

  For some reason, it would not work with Upgrade vs Update and I had to add this to main loop:


  if (!client.connected()) {
      reconnect();
    }


    Serial.println(F("done with if not client.connect"));

    // Added these 4 lines for Update subscription to work, Upgrade never worked
    delay(500);
    client.subscribe("wemosTestClient/setSleepTime");
    client.loop();
    client.subscribe("wemosTestClient/Update");
    client.loop();

  {
  "firmware": [
    {
      "": "0.0.0.2",
      "platforms": "2",
      "customers": "B&T",
      "description": "Home Weather Nodes",
      "url": "http://HomePi/WeatherNodes/WeatherNode_0.0.0.2",
      "build_epoch": "",
      "md5": "26800fc59dc5091d06e6c01556f0e551",
      "prereq": "0.0.0.1"


  For now, just use flat txt with  string, add other items and format in json later

  Using this link as a starting point (already had it working in POC sketch httpUpdate4.ino
  https://www.bakke.online/index.php/2017/06/02/self-updating-ota-firmware-for-esp8266/

  that example uses MAC address to identify the unique sketch, I'll try using the
  pubsub ClientID and using node red to tell the device when to check for an update. this should
  default to "do not check" on bootup. later add md5 checksum check -
  if the checksum is the same, do not upgrade and send message "s same,not upgrading"

  07/27/2018
  Creating WeatherNode_0003, this marks where we have a working POC OTA described above.
  For now it checks updates every time through the loop, this  will start cleaning up
  the code then add node-red and pubsub callbacks to initiate the upgrade

  Upto 0005 due to testing and cleanup

  Steps to upgrade:
  1: Create new build with changes and updated  string
  2: Export binary and copy it to directory as (example)
  pi@MesaPi1:/var/www/html/weathernodes/wemosTestClient $ ls wemosTestClient.bin
  wemosTestClient.bin
  example: firmware image trying to upload http://192.168.100.238/weathernodes/wemosTestClient/wemosTestClient.bin
  NOTE: May change this to  include  string in build ie wemostTestClient_0004.bin
  Later will have one weatherNode build once config file is done.
  3: change . file to updated  string

  V0007A todo
  1: update version function to strip dotted notation
  2: Add range sensor callibration nodes
  3: implement function for manufacturing /callibration mode
  4: Look at simplyfing lcd/distance sensor functions

V0007G
1: Version updated to include dotted seperator and functions to parse the version string
2: Implemented manufacturing/callibration mode, currently used for setSleeptime and ADC setting
3: Rewrote Update/Upgrade function to not use String at all - only string functions
4: Striped out as much String as I could from entire program and insetertd F macro everywhere
TODO
1: implement range sensor callibration
2: implement "upgrade force" to force a downgrade
3: implement "ESP.restart() in NodeRed
4: Config File

V 0.0.0.708 (H is 8th letter in alphabet
implement range sensor callibration
maxPingDist implemented in .708

V 0.0.0.708.git
first stab at using Git

V 0.0.0.709
1: Made LCD display real time measurments, rather than last taken when wifi was up 
 this was done in v 0.0.0.708.git
2: Had a millis() rollover issue in Toggle LCD (crashed every 49.7 days). Got rid of these lines
if (millis() >= pingTimer) { //pingSpeed millis since last ping; do another ping
    
    pingTimer += pingSpeed;   // set the next ping time

02/02/2019
New repo for .709. Going to add ability to specify upgrade file in NodeRed rather than just
using the auto-upgrade. NodeRed should use a "file picker" widgit

03/02/2018
Force upgrade implemented and deployed. No "file picker" bit does allow to force a downgrade by not 
doing a version check.
Adding function declarations at top of file (may be later moved ot .h file) and will move setup and loop 
to above other functions -> 0.0.0.710
 ***************************************************************************************************/


#include <stdio.h> 
#include <stdlib.h> 
#include <ESP8266WiFi.h>
#include <PubSubClient.h>  // for mqtt
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>  // This library is already built in to the Arduino IDE
#include <LiquidCrystal_I2C.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <NewPing.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
//#include <string.h>
//#include "WifiConfig.h"  Maybe try editing this to add wemos ?
#include <NtpClientLib.h>
#include <cactus_io_BME280_I2C.h>
//#define DEBUG 1
//#define Callback_Debug
#define NTP_ON 1
//#define SERIAL_ON
extern "C" {
#include "gpio.h"
}

extern "C" {
#include "user_interface.h"
}

//*************************** Station and Platform definitions ********************
#define Wemos 1
//#define TBOffice  1
//#define BethOffice 1
//#define Liv_Patio  1
// #define Danube 1  // The two outside temp sensor module

//#ifdef TBOffice
 //  #define Danube
//#elif defined Liv_Patio
#ifdef Liv_Patio
   #define Danube
#endif

//*************************** End Station and Platform definitions ****************

/*******************************************************************************************
*                                  Function Definitions                                    *                                     
*******************************************************************************************/
 
//********************************* BME Functions ******************************************
char* getBMETemp(BME280_I2C &theBME);
char* getBMEHumidity(BME280_I2C &theBME);
char* getBMEPressure(BME280_I2C &theBME);
//********************************* End BME Functions **************************************

//*********************************** Dallas OneWire Functions *******************************
char* getTubTemp();
char* getOutTemp();
//*********************************** End Dallas OneWire Functions **************************

//************************************* Battery / Charging functions *****************************
void checkChargerStatus(void);
float getBatteryLevel();
//************************************* End Battery / Charging functions *************************

//************************************* Upgrade Functions ****************************************
char* LastcharDel(char* name);
int compareSubstr(char *substr_version1, char *substr_version2, 
                  int len_substr_version1, int len_substr_version2);
int compareVersion(char* version1, const char* version2);
void checkForUpdates(bool force);  //Checks upgrade image version, force will force an upgrade
//********************************** End Upgrade Functions ****************************************   

//************************************* PubSub functions ******************************************
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
//********************************* End PubSub functions ******************************************



//*************************************** NTP and WiFi Event Functions *********************************
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo);
void onSTADisconnected(WiFiEventStationModeDisconnected event_info);
void processSyncEvent(NTPSyncEvent_t ntpEvent);
//************************************ End  NTP and WiFi Event Functions *********************************


/**************************************************************************************************
 *                                 Globals and Constants                                          *
 **************************************************************************************************/

static const byte timeZone = -8; // PST

/********************************** OLD PIN DEFINITIONS **********************************************
static const byte LCD_PIN =  D0;     // gate pin to control LCD was D3 10K ohm resistor
static const byte DST_PIN =  D5;     // gate pin to control distance sensor was D4 10K ohm resistor
static const byte SCL_PIN =  D1;     // i2c SCL
static const byte SDA_PIN =  D2;     // i2c SDA
static const byte ECHO_PIN = D7;     // USS ECHO Pin
static const byte TRIG_PIN = D8;     // USS TRIG Pin
static const byte ONEB_PIN = D6;     // One Wire buss 4.75K ohm resistor
static const byte PWR_PIN =  D3;     // Batt Charging yes/no was D0
static const byte DNE_PIN =  D4;     // Batt Charged yes/no was D0
static const byte BATT_PIN = A0;     // Batt Level 220k ohm resistor
************************************  END PIN DEFINITIONS *****************************************/





//*************************************** NEW PIN DEFINITIONS **************************************
  //  Both npn gates are not being used so remove them freeing up pins D0 and D5
  // D4 was  used batt charge status but it is also the internal led pin
  // Now use D0 for batt charge status (to row 20) and route D04 to opposite row 20) and D5 to row 21
  // for future use
  static const byte DNE_PIN =  D0;     //  Batt Charged yes/no was D4
  //static const byte DST_PIN =  D5;   // Not used, Terminated on row 22
  static const byte SCL_PIN =  D1;     // i2c SCL
  static const byte SDA_PIN =  D2;     // i2c SDA
  static const byte ECHO_PIN = D7;     // USS ECHO Pin
  static const byte TRIG_PIN = D8;     // USS TRIG Pin
  static const byte ONEB_PIN = D6;     // One Wir e buss 4.75K ohm resistor
  static const byte PWR_PIN =  D3;     // Batt Charging yes/no was D0
  //static const byte DNE_PIN =  D4;     // Not used, terminated on row 20
  static const byte BATT_PIN = A0;     // Batt Level 220k ohm resistor
//************************************ END NEW PIN DEFINITIONS *****************************************


//******************************* LCD & Dist Sensor transistor gate pins  ******************************
//                                Deprecated, pins recovered
//static const byte DST_PIN = D0;  // pin to drive gate pin of 2n222 for ultrasonic sensors - swapped with LCD pin for Ver L
//static const byte LCD_PIN = D4;   // pin to drive gate pin of 2n222 for LCD on/off - swapped with USS pin for Ver L
//*********************************************** End gate pin section **************************************


//*************************************** BTTN_PIN Push LCD Section ****************************************************
unsigned long lcdTurnedOnAt; // when lcd was turned on
int turnOffLcdDelay = 5000; // turn off LED after this time
bool lcdReady = false; // flag for when BTTN_PIN is let go
bool lcdState = false; // for LCD is on or not.
//*************************************** END BTTN_PIN Push LCD Section ***********************************************


//*************************************** HC-S04 UltraSonic Sensor ***************************************************
unsigned long sensorInRangeMillis; // when in sensor range
byte pingMaxDist = 60;
byte MIN_DIST = 20;
int pingSpeed = 500;
NewPing sonar(TRIG_PIN, ECHO_PIN, pingMaxDist);
//*************************************** END HC-S04 UltraSonic Sensor ***********************************************


//********************************************* IP, HOSTNAME WIFI  ********************************
// Static IP Addressing
#ifdef Wemos
IPAddress ip(192, 168, 100, 139);
#elif defined TBOffice
IPAddress ip(192, 168, 100, 181);
#elif defined BethOffice
IPAddress ip(192, 168, 100, 123);
#elif defined Liv_Patio
IPAddress ip(192, 168, 100, 137);
#endif
IPAddress gateway(192, 168, 100, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 100, 1);
const char* ssid = "BlueZebra";
const char* password = "jY7bzy2XJv";
//const char* IpAddr;
const char* Version = "0.0.0.710";
//const int  = 0001;
#ifdef Wemos
const char* ahostname = "Wemos";
#elif defined TBOffice
const char* ahostname = "TbOffice_Test5";
#elif defined BethOffice
const char* ahostname = "BethOffice";
#elif defined Liv_Patio
const char* ahostname = "Liv_Patio";
#endif

const char* fwUrlBase = "http://192.168.100.238/weathernodes/";

//************************* End IP, HOSTNAME,WIFI Defines *********************************



//*************************** ADC VOLTAGE *************************************************
// Unique for each chip, have to manually calibrate
#ifdef TbOfficeClient
  float ADC_ADJUST = 186;
#elif defined LIV_Patio
  float ADC_ADJUST = 183.51;
#else
  //float ADC_ADJUST = 175.33;  // Using a 220K resistor, 4.02 = 4.02 = exact at 4.15 V - now about .2 high
  float ADC_ADJUST = 175.33;
#endif
//*************************** END ADC VOLTAGE ***********************************************


//********************************** PUBSUB AND NODERED SETUP ********************************
#ifdef Wemos
WiFiClient(wemosTestClient);
PubSubClient client(wemosTestClient);
const char* Client = "wemosTestClient/";
#elif defined TBOffice
WiFiClient TbOfficeClient;
PubSubClient client(TbOfficeClient);
const char* Client = "TbOfficeClient/";
#elif defined Liv_Patio
WiFiClient(LivingPatioClient);
PubSubClient client(LivingPatioClient);
const char* Client = "LivingPatioClient/";
#endif
// RPI ADDRESS (MQTT BROKER - MOSQUITO)
const char* mqtt_server = "192.168.100.238";
//*************************************** END PUBSUB AND NODERED SETUP **************************

//************************************** WiFi MILLIS & SLEEP DEFFINITIONS **********************
bool awake = true;
unsigned long previousMillis = 0;
unsigned long sleepStartMillis = 0;
int sleeptime = 40; // initial sleep seconds, was byte
//byte sleeptime = 40; // range 1 - 255 or about 3 minutes sleeptime
byte numReadings = 2;
//*************************************** END MILLIS SLEEP DEF"S ********************************


// ********************************* ONE WIRE SENSORS **************************************************
#ifdef Wemos
DeviceAddress outAddr = {0x28, 0xFF, 0x4F, 0x57, 0x73, 0x16, 0x04, 0xBC}; // real wemos
//DeviceAddress outAddr={0x28,0xFF,0x3C,0x20,0x72,0x16,0x04,0x1A};   // test wemos
#elif defined TBOffice
DeviceAddress tubAddr = {0x28, 0xFF, 0x8D, 0xE7, 0x73, 0x16, 0x03, 0x0F};
DeviceAddress outAddr = {0x28, 0xFF, 0xCC, 0x88, 0x72, 0x16, 0x03, 0x8A}; // Clone address's
#elif defined BethOffice
//DeviceAddress outAddr={0x28,0xFF,0x3C,0x20,0x72,0x16,0x04,0x1A};  // Beth office outside old short
DeviceAddress outAddr = {0x28, 0x75, 0x64, 0x37, 0x08, 0x00, 0x00, 0x88}; // Beth office outside new long
#elif defined Liv_Patio
DeviceAddress outAddr = {0x28, 0xBA, 0x14, 0xB4, 0x07, 0x00, 0x00, 0x050}; // Patio address
DeviceAddress tubAddr = {0x28, 0xA2, 0x61, 0xC3, 0x06, 0x00, 0x00, 0x90}; //  tub address
#endif
//#define ONEB_PIN D4 // D2 on Wemos
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire SensorsPin(ONEB_PIN);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature Sensors(&SensorsPin);

//**************************** END ONE WIRE SETUP ********************************************

/**************************** MANUFACTURING Calibration DEFINES *******************************************
    This section used to subscribe to topics that will only be used when a new device is brought
    up  and needs to be calibration but will need to be changed later unless hardware readings change with age
    examples:
      1: ADJUST_ADC - only used to adjust the adc reading for vcc which varies for every board
      2: Temperature, Humidity, Pressure sensors. these change with every device and need to be callibrated

 *****************************************************************************************************/
#define ADJUST_ADC
bool callibratingMode = false;

//********************** I2C FOR LCD AND BME **********************************************************
#define BME_ADJUST -1
LiquidCrystal_I2C lcd(0x27, 20, 4);
BME280_I2C bme(0x76);  // I2C using address 0x76

//********************** END I2C SETUP ***************************************************************


/******************************************************************************************************
 *                        Functions                                                      
 ******************************************************************************************************/

//************************************** Batt Charg Functions ****************************************
void checkChargerStatus(void)
{
  char Topic[32];

  // Charging State
  strcpy(Topic, Client);
  strcat(Topic, "pwrState");
  if (digitalRead(PWR_PIN) == LOW)
  {
    client.publish(Topic, "Charging");
  }
  else
  {
    client.publish(Topic, "Not Charging");
  }

  // Battery Charged
  strcpy(Topic, Client);
  strcat(Topic, "dneState");

  if (digitalRead(DNE_PIN) == LOW)
  {
    client.publish(Topic, "Charged");
  }
  else
  {
    client.publish(Topic, "Not Charged");
  }
}

//*********************************** End Batt Charg Functions **************************************** 

//************************************* NTP and WIFI EVENT FUNCTIONS ********************************
// Start NTP only after IP network is connected
void onSTAGotIP(WiFiEventStationModeGotIP ipInfo) {
  Serial.printf("Got IP: %s\r\n", ipInfo.ip.toString().c_str());
}

// Manage network disconnection
void onSTADisconnected(WiFiEventStationModeDisconnected event_info) {
  Serial.printf("Disconnected from SSID: %s\n", event_info.ssid.c_str());
  Serial.printf("Reason: %d\n", event_info.reason);
  NTP.stop();

}
#ifdef NTP_ON
void processSyncEvent(NTPSyncEvent_t ntpEvent) {
  if (ntpEvent) {
    Serial.print(F("Time Sync error: "));
    if (ntpEvent == noResponse)
      Serial.println(F("NTP server not reachable"));
    else if (ntpEvent == invalidAddress)
      Serial.println(F("Invalid NTP server address"));
  }
  else {
    Serial.print(F("Got NTP time: "));
    Serial.println(NTP.getTimeDateString(NTP.getLastNTPSync()));
  }
}

boolean syncEventTRIGed = false; // True if a time event has been TRIGed
NTPSyncEvent_t ntpEvent; // Last Triggered event
#endif
//***************************** END NTP AND WIFI SECTION ******************************************
 
/****************** PUBSUB  MQTT/NODE RED FUNCTIONS ***************************************
  This functions is executed when some device publishes a message to a topic that your ESP8266 is subscribed to
  Change the function below to add logic to your program, so when a device publishes a message to a topic that
  your ESP8266 is subscribed you can actually do something
 ***********************************************************************************************/
 
/********************** Get temp readings *******************************************************
 *  1) Funtions to get BME readings and convert to char* for lcd print and mqtt publish
 *  2) Functions to get Dallas readings and conmvert to char* for lcd print and mqtt publish
 *************************************************************************************************/

//************************************ BME FUNCTIONS ********************************************
char* getBMETemp(BME280_I2C &theBME)
{
  float T = theBME.getTemperature_F();
  T = T - 4; //BME adjust temp - do this in node red?
  static char bmeTemperatureF[7];
  dtostrf(T, 6, 2, bmeTemperatureF);
  return bmeTemperatureF;
}

char* getBMEHumidity(BME280_I2C &theBME)
{
  float H = theBME.getHumidity();
  static char bmeHumidity[7];
  dtostrf(H, 6, 2, bmeHumidity);
  return bmeHumidity;
}

char* getBMEPressure(BME280_I2C &theBME)
{
  float P = (theBME.getPressure_MB() /(33.8639));
  static char bmePressure[7];
  dtostrf(P, 6, 2, bmePressure);
  return bmePressure;
}
//************************************ BME FUNCTIONS ********************************************

//********************************* Dallas One Wire Functions **************************************
char* getTubTemp()
{
#ifdef Danube  
  float tempF;
  char tubTempFString[6];
  
  Sensors.requestTemperatures();
//
  tempF = Sensors.getTempF(tubAddr);
  dtostrf(tempF, 3, 2, tubTempFString);
  return tubTempFString;
#endif
}
  
 char* getOutTemp()
{
    
  float tempF;
  char outTempFString[6];
  
 // Sensors.requestTemperatures();
  tempF = Sensors.getTempF(outAddr);
  tempF += 1.7;
  dtostrf(tempF, 3, 2, outTempFString);
  return outTempFString;
}

//****************************** END Dallas One Wire Functions **************************************

/*************************************************************************************
      takes two args, client and topic
      client = ahostname
      topic = the topic to publish

 *************************************************************************************/



void publishTopic(char topic[])
{
  char Topic[32];       // Make it big enough
  //char* Topic = ahostname;

  //strcpy(Topic, client);
  strcpy(Topic, Client);
  strcat(Topic, topic);
  client.publish(Topic, topic);
}



//***************************** HTTP OTA UPDATE FUNCTIONS  ***********************************************
char* LastcharDel(char* name)
{
    int i = 0;
    while(name[i] != '\0')
    {
        i++;
         
    }
    name[i-1] = '\0';
    return name;
}

// Functions to compare two versions using '.' separator
// utility function to compare each substring of version1 and 
// version2 
 int compareSubstr(char *substr_version1, char *substr_version2, 
                  int len_substr_version1, int len_substr_version2) 
 { 
     // if length of substring of version 1 is greater then 
     // it means value of substr of version1 is also greater 
     if (len_substr_version1 > len_substr_version2)  
        return 1; 
  
     else if (len_substr_version1 < len_substr_version2)  
        return -1; 
  
     // when length of the substrings of both versions is same. 
     else
     { 
        int i = 0, j = 0; 
  
        // compare each character of both substrings and return 
        // accordingly. 
        while (i < len_substr_version1) 
        { 
            if (substr_version1[i] < substr_version2[j]) return -1; 
            else if (substr_version1[i] > substr_version2[j]) return 1; 
            i++, j++; 
        } 
        return 0; 
     } 
 } 

 // function to compare two versions. 
int compareVersion(char* version1, const char* version2) 
{ 
    LastcharDel(version1);
    int len_version1 = strlen(version1); 
    int len_version2 = strlen(version2); 
  
    char *substr_version1 = (char *) malloc(sizeof(char) * 1000); 
    char *substr_version2 = (char *) malloc(sizeof(char) * 1000);     
  
    // loop until both strings are exhausted. 
    // and extract the substrings from version1 and version2 
    int i = 0, j = 0; 
    while (i < len_version1 || j < len_version2) 
    { 
        int p = 0, q = 0; 
  
        // skip the leading zeros in version1 string. 
        while (version1[i] == '0' || version1[i] == '.')  
           i++; 
  
        // skip the leading zeros in version2 string. 
        while (version2[j] == '0' || version2[j] == '.')  
           j++; 
  
        // extract the substring from version1. 
        while (version1[i] != '.' && i < len_version1)         
            substr_version1[p++] = version1[i++]; 
          
        //extract the substring from version2. 
        while (version2[j] != '.' && j < len_version2)         
            substr_version2[q++] = version2[j++];     
  
        int res = compareSubstr(substr_version1,  
                                substr_version2, p, q); 
  
        // if res is either -1 or +1 then simply return. 
        if (res) 
            return res; 
    } 
  
    // here both versions are exhausted it implicitly  
    // means that both strings are equal. 
    return 0; 
} 

void checkForUpdates(bool force) {
  char Topic[32];
  strcpy(Topic,Client);
  strcat(Topic, "lastUpgradeStatus");
  
  Serial.println(F("just entered chkforupdates, before char* ClientID"));
  //char ClientID[100];
  //strcpy(ClientID, Client);  // Why this ?

  Serial.println(F("Just before char* fwURL"));
  //char* fwURL;
  char fwURL[100];
  strcpy(fwURL, fwUrlBase);
  strcat(fwURL, Client);

  Serial.println(F("Just before char* fwVersionURL"));
  char fwVersionURL[100];
  strcpy(fwVersionURL, fwURL);
  strcat(fwVersionURL, ".version");
  
  Serial.println( F("Checking for firmware updates."));
  //Serial.print( "MAC address: " );
  //Serial.print(F( "ClientID: "));
  //Serial.println( ClientID );
  //Serial.println( mac );
  Serial.print(F( "Firmware version URL: " ));
  Serial.println(fwVersionURL );

  HTTPClient httpClient;
  httpClient.begin( fwVersionURL );
  int httpCode = httpClient.GET();
  if ( httpCode == 200 ) {
     char newFWVersion[100];
     strcpy(newFWVersion, httpClient.getString().c_str());

    Serial.print(F("Current firmware version: " ));
    //Serial.println( FW_VERSION );
    Serial.println(Version);
    Serial.print(F( "Available firmware version: " ));
    Serial.println( newFWVersion );

  // if force, int cmpver = 1, else int cmpVer = comparVersion(blah) 
  int cmpVer = compareVersion(newFWVersion,Version);

  Serial.print(F("cmpVer =: "));
  Serial.println(cmpVer);
  
  if (cmpVer == -1 && force == false) {
    Serial.println(F("New Version is older than current Version: NO UPGRADE"));
    client.publish(Topic, "OLDER VER, NO UPGRADE");
     publishUpgSwitchState(); 
    }
  else if (cmpVer == 0 && force == false) {
    Serial.println(F("New and Old versions are the same: NO UPGRADE"));
    client.publish(Topic, "Same Ver, NO UPGRADE");
     publishUpgSwitchState(); 
    
  }
  else{
    Serial.println(F("cmpVer == 1 so going to update"));
    //int newVersion = newFWVersion.toInt();
    //int newVersion = atoi(newFWVersion);
    //int oldVersion = atoi(Version);
    if (force == false) {
      client.publish(Topic, "New Ver, UPGRADE");
    }
    else {
      client.publish(Topic, "UPGRADE FORCE");
    }
    //if ( newVersion > oldVersion ) {
      Serial.println(F ("Preparing to update" ));
      char fwImageURL[100];
      strcpy(fwImageURL, fwURL);
      strcat(fwImageURL, Client);
      LastcharDel(fwImageURL);
      strcat(fwImageURL, ".bin");
      client.publish(Topic, "Downloading");
      Serial.print(F("firmware image trying to upload "));
      Serial.println(fwImageURL);
      publishUpgSwitchState();  // make sure switch is not stuck in upgrade status

      t_httpUpdate_return ret = ESPhttpUpdate.update( fwImageURL );

      switch (ret) {
        case HTTP_UPDATE_FAILED:
           client.publish(Topic, "DOWNLOAD FAILED");
           Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

        case HTTP_UPDATE_NO_UPDATES:
           Serial.println(F("HTTP_UPDATE_NO_UPDATES"));
            client.publish(Topic, "FILE NOT FOUND");
          break;
      }
    }
   // else {
     // Serial.println(F( "Already on latest version" ));
   // }
  }
  else {
    Serial.print(F("Firmware version check failed, got HTTP response code" ));
    Serial.println( httpCode );
    publishUpgSwitchState(); 
  }
  httpClient.end();
}


//************************** END checkForUpdates *********************************************************

//***************************** END HTTP OTA UPDATE FUNCTIONS  ***********************************************

//************************ PubSub Callbacks - Reconnect etc *******************************************

void callback(char* topic, byte* message, unsigned int length) {

  char TOPIC[64];
  char sleepTime[5];
  char PingMaxDist[5];
  char temp[4];
  char *ret;
  byte count = 0;
#ifdef ADJUST_ADC
  char adc_adjust[8];
#endif

#ifdef Calback_Debug
  Serial.println();
  Serial.print(F("!!!!!!!Message arrived on topic: "));
  Serial.println(topic);
  Serial.println(F("Building message"));
  Serial.print(F("Message =  "));
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
  }

  Serial.println();
#endif
 
  /********************  Upgrade section *************************/

  strcpy(TOPIC, "");
  strcpy(TOPIC, Client);
  strcat(TOPIC, "Update");

#ifdef Callback_Debug
  Serial.print(F("In Upgrade section, looking to match on "));
  Serial.println(TOPIC);
#endif
  if (strcmp(topic, TOPIC) == 0 ) {
#ifdef Callback_Debug
    Serial.println(F("!!!*** Matched on Update *******!!!"));
#endif
    if ((char)message[0] == '1') {
      // Add change update node state here

      Serial.println(F("New match print in Update !!!!!!!!!!!"));
      checkForUpdates(false);
      publishUpgSwitchState(); 
  }
#ifdef Callback_Debug
  else {
    Serial.print(F("TOPIC we are looking for is "));
    Serial.println(TOPIC);
    Serial.print(F("Topic we recieved is "));
    Serial.println(topic);

  }
#endif
}

/********************  Force Upgrade section *************************/

  strcpy(TOPIC, "");
  strcpy(TOPIC, Client);
  strcat(TOPIC, "forceUpdate");

#ifdef Callback_Debug
  Serial.print(F("In  Force Upgrade section, looking to match on "));
  Serial.println(TOPIC);
#endif
  if (strcmp(topic, TOPIC) == 0 ) {
#ifdef Callback_Debug
    Serial.println(F("!!!*** Matched on forceUpdate *******!!!"));
#endif
    if ((char)message[0] == '1') {
      // Add change update node state here

      Serial.println(F("New match print in forceUpdate !!!!!!!!!!!"));
      checkForUpdates(true);
      publishUpgSwitchState(); 
  }
#ifdef Callback_Debug
  else {
    Serial.print(F("TOPIC we are looking for is "));
    Serial.println(TOPIC);
    Serial.print(F("Topic we recieved is "));
    Serial.println(topic);

  }
#endif
}
 
/********************  Manufacturing/Callibration  section *************************/

  strcpy(TOPIC, "");
  strcpy(TOPIC, Client);
  strcat(TOPIC, "manufacturingMode");

#ifdef Callback_Debug
  Serial.print(F("In  Manufacturing section, looking to match on "));
  Serial.println(TOPIC);
#endif
  if (strcmp(topic, TOPIC) == 0 ) {
#ifdef Callback_Debug
    Serial.println(F("!!!*** Matched on manufacturingMode *******!!!"));
#endif
    if ((char)message[0] == '1') {
      
       Serial.println(F("****************Setting CallibratingMode to true*********"));
        callibratingMode = true;
     }  
     else {
        Serial.print(F("Did not match on "));
        Serial.println(TOPIC);
        Serial.print(F("Topic we recieved is "));
        Serial.println(topic);
    //callibratingMode = !callibratingMode;
        Serial.println(F("Setting Callibration mode to FALSE"));
        callibratingMode = false;
     }
   }
//#ifdef ADJUST_ADC // This will change to if(manufacturingMODE) etc under next section
if(callibratingMode){
  Serial.println(F("********* APPARENTLY IN CALLIBRATING MODE ********"));

  
  /*********************** setSleepTime section ********************************/
  strcpy(TOPIC, Client);
  strcat(TOPIC, "setSleepTime");

  if (strcmp(topic, TOPIC) == 0 ) {
    Serial.println(F("!!!*** Matched on setSleepTime ********!!!"));

    Serial.print(F("Current sleeptime = "));
    Serial.println(sleeptime);
    Serial.print(F("changing sleeptime to: "));
    for (int i = 0; i < length; i++) {
      sleepTime[i] = (char)message[i];
      count++;
    }

    sleepTime[count] = '\0';
    Serial.println(sleepTime);
    sleeptime = atoi(sleepTime);  // Change global sleeptime

  }  // endif strcmp(topic,TOPIC) == 0) {
 
  Serial.println(topic);

/************************** espRestart section ******************************/
strcpy(TOPIC, Client);
  strcat(TOPIC, "espRestart");
  if (strcmp(topic, TOPIC) == 0 ) {
    Serial.println(F("!!!*** Matched on espRestart ********!!!"));
     if ((char)message[0] == '1') {
      // Add change update node state here
    Serial.println(F("!!!!!! Restarting ESP !!!!!!!!!!!!!!!!!!!!!"));
   publishEspRestartSwitchState();  // turn restart switch to off position
   ESP.restart();
     } // endif message == 1
  }  // endif strcmp(topic,TOPIC) == 0) {
  
  /************************* End espRestart Section *****************************/
  
 //*********************** pingMaxDist section ********************************
  strcpy(TOPIC, "");
  strcpy(TOPIC, Client);
  strcat(TOPIC, "setPingMaxDist");

  if (strcmp(topic, TOPIC) == 0 ) {
    Serial.println(F("!!!*** Matched on setPingMaxDist ********!!!"));

    Serial.print(F("Current pingMaxDist = "));
    Serial.println(pingMaxDist);
    Serial.print(F("changing pingMaxDist to: "));
    for (int i = 0; i < length; i++) {
      PingMaxDist[i] = (char)message[i];
      count++;
    }

    PingMaxDist[count] = '\0';
    Serial.println(PingMaxDist);
    pingMaxDist = atoi(PingMaxDist);  // Change global pingMaxDist

  }  // endif strcmp(topic,TOPIC) == 0) {
  // Serial.print(F("Just before checking for upgrade, topic = "));
  Serial.println(topic);
//*******************************************************************************************
  /************************** ADC ADJUST section *****************************************************************
      tweak the ADC_ADJUST float for better VCC measurments (every circuit is a bit different)
      From the ADC adjust section the default is
      float ADC_ADJUST = 175.33;  // Using a 220K resistor, 4.02 = 4.02 = exact at 4.15 V - now about .2 high
   ***************************************************************************************************************/

  //strcpy(TOPIC, "");
  strcpy(TOPIC, Client);
  strcat(TOPIC, "setAdcAdjust");

#ifdef Callback_Debug
  Serial.print(F("In callback setAdcAdjust section, looking to match on "));

  Serial.println(TOPIC);
#endif
  if (strcmp(topic, TOPIC) == 0 ) {
#ifdef Callback_Debug
    Serial.println(F("!!!*** Matched on setAdcAdjust *******!!!"));
#endif

    dtostrf(ADC_ADJUST, 7, 2, adc_adjust);
     Serial.print(F("Current ADC_ADJUST = "));
    Serial.println(adc_adjust);

    Serial.print(F("New adc_adjust value is: "));
    for (int i = 0; i < length; i++) {
      adc_adjust[i] = (char)message[i];
      //setAdcAdjust[i] = (char)message[i];
      count++;
    }

    adc_adjust[count] = '\0';
    Serial.println(adc_adjust);
    if (ADC_ADJUST != atof(adc_adjust)) {
      Serial.print(F("Changing ADC_ADJUST to : "));
      Serial.println(adc_adjust);
      Serial.println(atof(adc_adjust));
      ADC_ADJUST = atof(adc_adjust);  // Change ADC_ADJUST - seems to almost lock the program and free heap drops dramatically
    }
    else {
      Serial.println(F("No Change to ADC_ADJUST"));
      //callibratingMode = !callibratingMode;
    }
  }
}  // just added this one for if callibrating mode
  else {
    #ifdef DEBUG
    Serial.print(F("TOPIC we are looking for is "));
    Serial.println(TOPIC);
    Serial.print(F("Topic we recieved is "));
    Serial.println(topic);
   #endif
    }
  }
//#endif



void reconnect() {

  char Topic[64];
  strcpy(Topic, Client);
  //strcat(Topic, "#");

  // Loop until we're reconnected
  while (!client.connected()) {
    #ifdef DEBUG
    Serial.println(F("Attempting MQTT connection..."));
    #endif
    // Attempt to connect
    if (client.connect(Client)) {
      #ifdef DEBUG
      Serial.print(F("!!!!! In recconect, connected to "));
      Serial.println(Client);
      Serial.print(F("Building Topic with Client "));
      Serial.println(Topic);
      Serial.println(Client);
      #endif

/****************  Subscribe to Topic setSleeptime **********************/

      strcat(Topic, "setSleepTime");
      #ifdef DEBUG
      Serial.print(F("now added setSleepTime, Topic = "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(100);
      #ifdef DEBUG
      Serial.print(F("subscribed to "));
      Serial.println(Topic);
      #endif


/****************  Subscribe to Topic espReload ****************************/
      strcpy(Topic, Client);
      strcat(Topic, "espRestart");
      Serial.print(F("now added espRestart, Topic = "));
      Serial.println(Topic);
      #ifdef DEBUG
      Serial.print(F("now added espRestart, Topic = "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(200);
      client.loop();
      #ifdef DEBUG
      Serial.print(F("subscribed to "));
      Serial.println(Topic);
      #endif
      
/*************************Subscribe to Topic Upgrade ***********************************/

      client.loop();
     #ifdef DEBUG
      Serial.println(F("Attempting to subscribe to Upgrade Topic"));
      #endif
      strcpy(Topic, Client);
      strcat(Topic, "Update");
      #ifdef DEBUG
      Serial.print(F("now added Upgrade, Topic = "));
      Serial.println(Topic);
      //client.subscribe(Topic);
      Serial.print(F("Subscribing to topic "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(200);
      client.loop();
      client.loop();


/*************************Subscribe to Topic forceUpgrade ***********************************/

      client.loop();
     #ifdef DEBUG
      Serial.println(F("Attempting to subscribe to forceUpgrade Topic"));
      #endif
      strcpy(Topic, Client);
      strcat(Topic, "forceUpdate");
      #ifdef DEBUG
      Serial.print(F("now added forceUpgrade, Topic = "));
      Serial.println(Topic);
      //client.subscribe(Topic);
      Serial.print(F("Subscribing to topic "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(200);
      client.loop();
      client.loop();
       /*************************Subscribe to Topic manufacturingMode ***********************************/

      client.loop();
    #ifdef DEBUG
      Serial.println(F("Attempting to subscribe to ManufacturingMode Topic"));
    #endif
      strcpy(Topic, Client);
      strcat(Topic, "manufacturingMode");
     #ifdef DEBUG
      Serial.print(F("now added manufacturing, Topic = "));
      Serial.println(Topic);
      //client.subscribe(Topic);
      Serial.print(F("Subscribing to topic "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(200);
      client.loop();
      client.loop();


 /*************************Subscribe to Topic pingMaxDist ***********************************/

      client.loop();
    #ifdef DEBUG
      Serial.println(F("Attempting to subscribe to setPingMaxDist Topic"));
    #endif
      strcpy(Topic, Client);
      strcat(Topic, "setPingMaxDist");
    #ifdef DEBUG
      Serial.print(F("now added setPingMaxDist, Topic = "));
      Serial.println(Topic);
      client.subscribe(Topic);
      Serial.print(F("Subscribing to topic "));
      Serial.println(Topic);
      #endif
      client.subscribe(Topic);
      delay(200);
      client.loop();
      client.loop();

      /*************************Subscribe to Topic setAdcAdjust ***********************************/
#ifdef ADJUST_ADC

      Serial.println(F("Attempting to subscribe to setAdcAdjust Topic"));
      strcpy(Topic, Client);
      strcat(Topic, "setAdcAdjust");
      Serial.print(F("now added setAdcAdjust, Topic = "));
      Serial.println(Topic);
      //client.subscribe(Topic);
      Serial.print(F("Subscribing to topic "));
      Serial.println(Topic);
      client.subscribe(Topic, 1);
      delay(200);
      client.loop();
      client.loop();

#endif
    }

    else {
      Serial.print(F("failed, rc="));
      Serial.print(client.state());
      Serial.println(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }

  }
}

//******************* END PUBSUB FUNCTIONS ************************************


//******************* GET BATTERY LEVEL  **************************************
float getBatteryLevel() {
  float level = analogRead(BATT_PIN) / ADC_ADJUST;
#ifdef SERIAL_ON
  Serial.print("Battery level: "); Serial.print(level); Serial.println("%");
#endif
  return level;

}

//************************ END GET BATTERY LEVEL *********************************


//*********************** OTA SETUP ******************************************
//  Can I move stuff from Setup to here?
//************************* END OTA SETUP ***************************************



//********************************** LCD Functions ***************************

void lcdOn(void)
{
  //digitalWrite(LCD_PIN, HIGH); // added this using transistor
  //lcd.init();
  lcd.backlight();
  lcd.display();

}

void lcdOff(void)
{
  lcd.noDisplay();
  lcd.noBacklight();
  //digitalWrite(LCD_PIN, LOW);  // added this using transistor
}

//**************************** LCD Setup *********************************
//                    Run's once in setup
//     Display hostname,  and ip info then turns off display
//***********************************************************************

void lcdSetup(void)
{
  //digitalWrite(LCD_PIN, HIGH);
  lcd.init();   // initializing the LCD
  lcdOn();
  lcd.setCursor(0, 0);
  lcd.print(F("Hostname"));
  lcd.print(ahostname);
  lcd.setCursor(0, 1);
  lcd.print(F("Ver: "));
  lcd.print(Version);
  lcd.setCursor(0, 3);
  lcd.print(F("Ip "));
  lcd.print (WiFi.localIP());
  delay(5000);
  lcdOff();
}



//*********************** lcdDisplayTemps2 *********************
//             Displays Sensor readings when BTTN_PIN pressed
//**************************************************************
void lcdDisplayTemps2()
{
  // Add LCD state machine stuff here
  //digitalWrite(LCD_PIN, HIGH);
  char tubTempF[6];
  char outTempF[6];
  char*  bmeTemperatureF; 
  char* bmePressure;
  char* bmeHumidity;

  bme.readSensor();
  bme.readSensor();

  bmeTemperatureF = getBMETemp(bme);
  bmePressure = getBMEPressure(bme);
  bmeHumidity = getBMEHumidity(bme);
 
  //lcdOn();
  lcd.clear();
  lcd.setCursor(0, 0);
 

  Sensors.requestTemperatures();
  #ifdef Danube
     //tubTempF = getTubTemp;
     lcd.print(F("Tub Temp: "));
     lcd.print(getTubTemp();
     lcd.print(" *F");
  #else 
     lcd.print(F("B&T Home TMP Guage"));
  #endif

 
  lcd.setCursor(0, 1);
  lcd.print(F("Out Temp: "));
  lcd.print(getOutTemp());
  lcd.print(F(" *F"));

  //Inside Temp
  lcd.setCursor(0, 2);
  lcd.print(F("In  Temp: "));
  lcd.print(bmeTemperatureF);
  lcd.print(F(" *F"));
  // Pressure
  lcd.setCursor(0, 3);
  lcd.print(F("P:  "));
  lcd.print(bmePressure);
  //Humidity
  lcd.print(F(" H: "));
  lcd.print(bmeHumidity);
}


//***************** END LCD FUNCTIONS **********************************
#ifdef SERIAL_ON
void serialPrintReadings(void)
{
  serialPrintNTP();
  serialPrintBme();
  serialPrintHeap();
  serialPrintResetReason();
  serialPrintVcc();
}
#endif

void publishReadings(void)
{
  // MQTT Publish readings
  publishBme();
  publishVcc();
  publishStaInfo();
  publishNTP();
  publishHeap();
  #ifndef Liv_Patio //FIXME, WHY IS THIS NECESSARY??????????
  
  publishResetReason();
  #endif
  publishDallasTemps();
  checkChargerStatus();
  publishSleepTime();
  publishPingMaxDist();
  publishSleepState(0);
  publishADC_ADJUST();
}

#ifdef SERIAL_ON
void serialPrintBme(void)
{
  bme.readSensor();
  Serial.print(bme.getPressure_MB() / (33.8639)); Serial.print(F("\t\t"));
  Serial.print(bme.getHumidity()); Serial.print(F("\t\t"));
  Serial.print(bme.getTemperature_C()); Serial.print(F(" *C\t"));
  Serial.print(bme.getTemperature_F()); Serial.println(F(" *F\t"));
}

#endif

#ifdef SERIAL_ON
void serialPrintNTP(void)
{
  Serial.print(NTP.getTimeDateString()); Serial.print(F(" "));
  Serial.print(NTP.isSummerTime() ? "Summer Time. " : "Winter Time. ");
  Serial.print(F("WiFi is "));
  Serial.print(WiFi.isConnected() ? "connected" : "not connected"); 
(F(". "));
  Serial.print(F("Uptime: "));
  Serial.print(NTP.getUptimeString()); Serial.print(F(" since "));
  Serial.println(NTP.getTimeDateString(NTP.getFirstSync()).c_str());
  Serial.println("Free Heap      = " + String(ESP.getFreeHeap()));
  Serial.println("Last Reset Reason      = " + String(ESP.getResetReason()));
}

#endif

#ifdef SERIAL_ON
void serialPrintHeap(void)`

{
  Serial.println("Free Heap      = " + String(ESP.getFreeHeap()));
}
#endif

#ifdef SERIAL_ON
void serialPrintResetReason(void)
{
  Serial.println("Last Reset Reason      = " + String(ESP.getResetReason()));
}
#endif

#ifdef SERIAL_ON

void serialPrintVcc(void)
{
  char VCC[7];
  float vdd = getBatteryLevel();
  dtostrf(vdd, 6, 2, VCC);
  Serial.print(F("VCC voltage = "));
  Serial.print(VCC);
  Serial.println(F(" Vdc\t"));

}
#endif

void publishVcc(void)
{
  char VCC[7];
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "VCC");

  float vdd = getBatteryLevel();
  dtostrf(vdd, 6, 2, VCC);
  //client.publish("/TbOfficeClient/VCC", VCC);
  client.publish(Topic, VCC);
}

void publishADC_ADJUST(void) {

  char adc_adjust[8];
  char Topic [32];
  dtostrf(ADC_ADJUST, 7, 2, adc_adjust);

  strcpy(Topic, Client);
  strcat(Topic, "ADC");
  client.publish(Topic, adc_adjust);

}


void publishSleepTime(void) {
  char sleepTime[12];
  char Topic[32];
  char temp[4];

  strcpy(sleepTime, itoa(sleeptime, temp, 10));
  strcpy(Topic, Client);
  strcat(Topic, "sleepTime");

  client.publish(Topic, sleepTime);
  Serial.print(F("sleeptime now equals "));
  Serial.println(sleeptime);
  // client.publish(Topic, sleeptime);

}


void publishPingMaxDist(void) {
  char PingMaxDist[12];
  char Topic[32];
  char temp[4];

  strcpy(PingMaxDist, itoa(pingMaxDist, temp, 10));
  strcpy(Topic, Client);
  strcat(Topic, "pingMaxDist");

  client.publish(Topic, PingMaxDist);
  Serial.print(F("pingMaxDist now equals "));
  Serial.println(pingMaxDist);
  // client.publish(Topic, sleeptime);

}

void pubTopic(char Topic[], char Payload[])
{
  char topic[32];
  char payload[32];
  strcpy(topic, Client);
  strcpy(payload, Payload);
  strcat(topic, Topic);
  client.publish(topic, payload);
}

//FIXME - make pubTopic work!
char* makeTopic(char* value)
{
  // char topic[32];
  char* topic;
  strcpy(topic, Client);
  strcat(topic, value);
  return (topic);
}

void publishUpgSwitchState(void)
{
  char State[2];
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "upgSwitchState");
  client.publish(Topic,"0");

  strcpy(Topic, Client);
  strcat(Topic, "forceUpgSwitchState");
  client.publish(Topic,"0");
 }

void publishEspRestartSwitchState(void)
{
  char State[2];
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "espRestartSwitchState");
  client.publish(Topic,"0");
}

 void publishMfgSwitchState(void)
{
  char State[2];
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "MfgSwitchState");
  client.publish(Topic, "0");
 }




void publishStaInfo(void)
{
  char Topic[32];

  // Publish Version
  strcpy(Topic, Client);
  strcat(Topic, "Version");
  client.publish(Topic, Version );

  // Publish Hostname
  strcpy(Topic, Client);
  strcat(Topic, "Hostname");
  client.publish(Topic, ahostname);

  // Publish IP
  strcpy(Topic, Client);
  strcat(Topic, "IP");
  client.publish(Topic,  WiFi.localIP().toString().c_str());

  // Publish MAC
  strcpy(Topic, Client);
  strcat(Topic, "MAC");
  client.publish(Topic, WiFi.macAddress().c_str());

  // Publish ADC adjust value
  // publishADC_ADJUST();

}


void publishBme(void)
{
  char Topic[32];
 
  // supposedly BME re-uses the last reading so take 2 readings
  bme.readSensor();
  bme.readSensor();
  bme.readSensor();
 // Create character strings for MQTT publish
  
  char* bmeTemperatureF;
  char* bmePressure;
  char* bmeHumidity;

  bmeTemperatureF = getBMETemp(bme);
  bmePressure = getBMEPressure(bme);
  bmeHumidity = getBMEHumidity(bme);

  //String currDate = NTP.getTimeDateString();

  strcpy(Topic, Client);
  strcat(Topic, "temperature");
  client.publish(Topic, bmeTemperatureF);

  strcpy(Topic, Client);
  strcat(Topic, "humidity");
  client.publish(Topic, bmeHumidity);

  strcpy(Topic, Client);
  strcat(Topic, "pressure");
  client.publish(Topic, bmePressure);

}

void publishBme_olde(void)
{
  char Topic[32];

 
  // supposedly BME re-uses the last reading so take 2 readings
  bme.readSensor();
  bme.readSensor();
  bme.readSensor();
  // Get floats from BME
  float P = bme.getPressure_MB() / 33.8639;
  float T = bme.getTemperature_F();
  float H = bme.getHumidity();
  T = T - 2;  // BME adjust temp

  // Create character strings for MQTT publish
  static char bmeTemperatureF[7];
  static char bmePressure[7];
  static char bmeHumidity[7];

  // Convert Float (returned from bme.readsensor)  to String to publish
  dtostrf(T, 6, 2, bmeTemperatureF);
  dtostrf(P, 6, 2, bmePressure);
  dtostrf(H, 6, 2, bmeHumidity);

  //String currDate = NTP.getTimeDateString();

  strcpy(Topic, Client);
  strcat(Topic, "temperature");
  client.publish(Topic, bmeTemperatureF);

  strcpy(Topic, Client);
  strcat(Topic, "humidity");
  client.publish(Topic, bmeHumidity);

  strcpy(Topic, Client);
  strcat(Topic, "pressure");
  client.publish(Topic, bmePressure);

}


void publishNTP(void)
{

  char Topic[32];

  //  NTP returns type String, bummer!
  String currDate = NTP.getTimeDateString();
  String upTime = NTP.getUptimeString();
  String bootDate = (NTP.getTimeDateString(NTP.getFirstSync()).c_str());

  // Build Topic for Pubsub publish, Start with Client and add topic then publish Topic, value

  // uptime
  strcpy(Topic, Client);
  strcat(Topic, "upTime");
  client.publish(Topic, upTime.c_str());

  // Current time
  strcpy(Topic, Client);
  strcat(Topic, "currTime");
  client.publish(Topic, currDate.c_str());

  // Boot Time
  strcpy(Topic, Client);
  strcat(Topic, "bootTime");
  client.publish(Topic, bootDate.c_str());
}


void publishHeap(void)
{
  char freeHeap[8];
  char Topic[32];

  float FH = ESP.getFreeHeap();
  dtostrf(FH, 9, 2, freeHeap);

  strcpy(Topic, Client);
  strcat(Topic, "freeHeap");

  client.publish(Topic, freeHeap);

}



void publishResetReason(void)
{
  char Topic[32];
  String lastResetReason = (ESP.getResetReason().c_str());

  strcpy(Topic, Client);
  strcat(Topic, "lastResetReason");

  client.publish(Topic, lastResetReason.c_str());


}

// Let NodeRed know we are awake

// Template for config change config options
void publishSleepState(int state)
{
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "SleepState");

  if (state == 0) {
    client.publish(Topic, "Awake");
#ifdef SERIAL_ON
    Serial.print(F("Topic = "));
    Serial.println(Topic);
    Serial.println(F("Sleep State message = Awake"));
#endif
  }
  else {
    client.publish(Topic, "Sleeping");
#ifdef SERIAL_ON
    Serial.print(F("Topic = "));
    Serial.println(Topic);
    Serial.println(F("Sleep State message = Sleeping"));
#endif
  }

}

void publishImAsleep(void)
{
  char Topic[32];

  strcpy(Topic, Client);
  strcat(Topic, "ImAwake");

  client.publish(Topic, "ImAsleep");

  // #ifdef SERIAL_ON
  Serial.print(F("Topic = "));
  Serial.println(Topic);
  Serial.println(F("Awake message = ImAwake"));
  // #endif

}


void publishDallasTemps(void)
{
  char Topic[32];
  //strcpy(Topic, Client);
  float tempC;
  float tempF;

  ///char tubTempCString[6];
  char tubTempFString[6];

 // char outTmpCString[6];
  char outTmpFString[6];
  

  Sensors.requestTemperatures();
  do {
    //#ifdef Tub_Sensor

#ifdef Danube  // We have a second outside temp one wire sensor
    // Tub Temp
    
    tempF = Sensors.getTempF(tubAddr);
    dtostrf(tempF, 3, 2, tubTempFString);
#endif
    // Outside Temp
    tempF = Sensors.getTempF(outAddr);
    tempF += 1.7;
    dtostrf(tempF, 3, 2, outTmpFString);

  } while (tempC == 85.0 || tempC == (-127.0));

#ifdef Danube
  strcpy(Topic, Client);
  strcat(Topic, "Tub/temperature");
  //client.publish("/TbOfficeClient/Tub/temperature", tubTempFString);
  client.publish(Topic, tubTempFString);
 // client.publish(Topic, getTubTemp());

#endif
  strcpy(Topic, Client);
  strcat(Topic, "Out/temperature");
  client.publish(Topic, outTmpFString);
  //client.publish(Topic, getOutTemp());
}


unsigned int checkDistance()
{
  unsigned int uS = sonar.ping(); // Send ping, get ping time in microseconds (uS).
  uS = sonar.ping();
  unsigned int Tmp = uS / US_ROUNDTRIP_CM;  // US_ROUNDTRIP_CM included in lib ?


  //***** Find a way to ping x times, break if Distance in range
  if ( Tmp >= pingMaxDist || Tmp <= MIN_DIST) {
    lcdReady = false;
  }
  else {
    sensorInRangeMillis = millis();
    lcdReady = true;
 }
 return (Tmp);
}

void toggleLCD(void)
  {
    unsigned int dS;
    unsigned long currentMillis = millis();
    dS = checkDistance();

  // make sure this code isn't checked until after the range sensor is activated
  if (lcdReady) {
    lcdState = true;
    // save when the LED turned on
    lcdTurnedOnAt = currentMillis;
    // wait for next arm wave
    lcdReady = false;
    lcdOn();
    lcdDisplayTemps2();
    //  }
  }

  // see if we are watching for the time to turn off LCD
  if (lcdState) {
    // okay, lcd on, check for how long
    if ((unsigned long)(currentMillis - lcdTurnedOnAt) >= turnOffLcdDelay) {
      lcdState = false;
      //digitalWrite(ULT_LED, LOW);
      lcdOff();
    }
  }  
}


/**************************************************************************
                             SETUP!
 **************************************************************************/
void setup() {

  char Topic[32];
  
  
  //strcpy(sleeptime, "40");
  //pinMode(BTTN_PIN, INPUT_PULLUP);    
  pinMode(PWR_PIN, INPUT_PULLUP);   
  pinMode(DNE_PIN, INPUT_PULLUP);  
  Serial.begin(115200);
  delay(10);
  Wire.begin();
  WiFi.hostname( ahostname );
  Sensors.begin(); // IC Default 9 bit. If you have troubles consider upping it 12. Ups the delay giving the IC more time to process the temperature measurement

  // **************** WIFI connect section ***************
  Serial.println();
  Serial.print(F("Connecting to "));
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);   // try uncommenting if OTA does not work
  WiFi.persistent(false);
  WiFi.config(ip, gateway, subnet, dns);
  WiFi.begin(ssid, password);

  Serial.print(F("Connecting")); //vdd == getBatteryLevel();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("."));
    delay(200);
  }
  // Added this to circumvent Event stuff in VH
  NTP.begin("pool.ntp.org", timeZone, true);
  NTP.setInterval(63);


  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println(F("Connection Failed! Rebooting..."));
    delay(5000);
    ESP.restart();
  }

  lcdSetup();
  ArduinoOTA.begin();

  Serial.println(F(""));
  Serial.println(F("WiFi connected"));

  

  // Printing the ESP IP address
  Serial.println(WiFi.localIP());

  Serial.print(F(": "));
  Serial.println();

  //BME280 to serial
  Serial.println(F("Bosch BME280 Barometric Pressure - Humidity - Temp Sensor | cactus.io"));

  if (!bme.begin()) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring!"));
    // while (.8);
  }

 // bme.setTempCal(BME_ADJUST);  // Adjust up or down

  Serial.println(F("Pressure\tHumdity\t\tTemp\t\tTemp"));


  // MQTT setup
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  if (!client.connected()) {
      reconnect();
  }
  Serial.println(F(" In setup, just set lcdState to 1"));
 
  strcpy(Topic, Client);
  Serial.print(F("In setup, topic = "));
  Serial.println(Topic);
  strcat(Topic,"lastUpgradeStatus");
  Serial.print(F("In setup, topic = "));
  Serial.println(Topic);
  client.publish(Topic, "IDLE");
  
}







/*****************************************************************************
                              MAIN LOOP
******************************************************************************/
void loop() {


  //Serial.println(F("At very top of loop"));
  unsigned long currentMillis = millis();
  unsigned int dS;
  sleepStartMillis = millis();
  
#ifdef NTP_ON
  if (syncEventTRIGed) {
    processSyncEvent(ntpEvent);
    syncEventTRIGed = false;
  }
#endif
  toggleLCD();  // check for turning lcd on or off on every loop
  int x;  // for loop counter
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  //Serial.println(F("Just before if awake is true"));
  if (awake == true)
  {
    // Serial.println(F("awake = true, getting to work"));
    Serial.println("Free Heap      = " + String(ESP.getFreeHeap(), DEC));

    Serial.println(F("starting Wifi"));
    Serial.print(F("Connecting to "));
    Serial.println(ssid);
    // try commenting if OTA does not work

    //WiFi.config(ip, gateway, subnet, dns);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(F("."));
    }
    if (!client.connected()) {
      reconnect();
    }
    #ifdef DEBUG
    Serial.println(F("done with if not client.connect"));
   
    Serial.println(F("done with client.connect to "));
    #endif
    Serial.println(F("starting Client.loop()"));
    client.loop();

    // This delay added so subscribe works, it used to work without it!
    delay(100);

    Serial.println("Free Heap      = " + String(ESP.getFreeHeap(), DEC));

    // Get x readings then sleep for 60 seconds

    for (x = 1; x <= numReadings; x++)
    {
      yield();
      // Serial Print data


      //FIXME: Why is this not using previous defined functions?
#ifdef SERIAL_ON
      serialPrintNTP();
      serialPrintBme();
      serialPrintHeap();
      serialPrintResetReason();
      serialPrintVcc();
      yield();
#endif
      // MQTT Publish
      publishReadings();
      yield();
    }// end for loop

    //******* reduced from delay(5000) to delay(3000) for OTA WDT
    delay(3000);
    publishSleepState(1);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);

    Serial.print(F("************ forceing modem sleep "));
    WiFi.forceSleepBegin();
    delay(1);
    // Serial.print(sleeptime);
    Serial.println(F("************"));
    Serial.print(F("Current sleeptime =  "));
    Serial.println(sleeptime);
    //publishImAsleep();
    //sleepStartMillis = millis();
    WiFi.forceSleepBegin();
    delay(1);
    // Serial.println(F("**************** Setting awake to False ***************"));
    awake = false;
    //e2 = WiFi.onStationModeDisconnected(onSTADisconnected);
  }// end if awake = true

  else {
    delay(1000);

   

    //******************** WAKING UP ************************************
    // had to add 10 seconds to sleeptime, not sure why
    if ((unsigned long)(currentMillis - previousMillis) >= (sleeptime * 1000 + 10000)) {
      //if ((unsigned long)(sleepStartMillis - previousMillis) >= sleeptime) {
      //  Serial.println(F("**************Time to Wake up and get to work!******************"));
      // Serial.println(F("************** awake = TRUE ******************"));
      awake = true;
      WiFi.forceSleepWake();
      previousMillis = millis();
    }

  }   // end loop
}

