#include <ESP8266WiFi.h>          
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <Servo.h>
#include <SimpleTimer.h>
#include <ESP8266HTTPClient.h>
#include <MD5Builder.h>
#include <RtcDS3231.h>
extern "C" {
  #include "user_interface.h"
}

Servo servo;
/*
 *const int PIN_LED = 2; // D4 on NodeMCU and WeMos. Controls the onboard LED.
 *
 * Trigger for inititating config mode is Pin D3 and also flash button on NodeMCU
 * Flash button is convenient to use but if it is pressed it will stuff up the serial port device driver 
 * until the computer is rebooted on windows machines.
 */
const int TRIGGER_PIN = D3; // D3 on NodeMCU and WeMos.
const int LED_PIN = D7; //Diod pin
const int MOTOR_PIN = D5; //Motor pin
const String DEV_VERSION = "1.0";
const int POWERCHECK_INTERVAL = 600000; // 10 min Check battery volage
const int SCHEDULE_INTERVAL = 60000; // 1 min Check shedule volage
const float VOLTAGE_THRESHOLD = 2710; // voltage threshold , mv
const int BROADCAST_INTERVAL = 5000; // 5 sec broadcastinterval, milisecond
unsigned int UPDPORT = 28031;

int REPOSITORY_INTERVAL = 120000; // 2 min Repository interval check, milisecond
char* REPOSITORY_HOST = "http://345.kiev.ua/1.php"; //Repository  full adress for remote job
String DID = ""; //device ID
String DHEX = ""; //Device md5 hex id based

/*
 * Alternative trigger pin. Needs to be connected to a button to use this pin. It must be a momentary connection
 * not connected permanently to ground. Either trigger pin will work.
 * const int TRIGGER_PIN2 = 13; // D7 on NodeMCU and WeMos.
*/

bool initialConfig = false; // Indicates whether ESP has WiFi credentials saved from previous session
ADC_MODE(ADC_VCC); //for voltage measuring
std::unique_ptr<ESP8266WebServer> server;
MD5Builder builder_md5;
SimpleTimer timer_remote;
SimpleTimer timer_vcc;
SimpleTimer timer_broadcast;
SimpleTimer timer_schedule;
WiFiUDP udp;
RtcDS3231 Rtc;

//-------------main functions----------------------------------------
  void doFeed(String sval) {
        int ival = sval.toInt();
        for(int a=0; a<ival; a++){
              for(int i=0; i<180; i++){
                 goblink(i);
                 servo.write(i);
                 delay(10);
                 }
                 for(int i=180; i>0; i--){
                 goblink(i);
                 servo.write(i);
                 delay(5);
                 }
                 delay(200); // for fill the bank))
        }
  }
  void goblink(int i){ 
    if ((i%10) == 0){ 
  bool state = digitalRead(LED_PIN);
  digitalWrite(LED_PIN, !state);
   }
  }
  void doblink(int pin, int count = 1, int timeout = 500){
    for(int i=0; i<count; i++){
         digitalWrite(pin, LOW);
         delay(timeout); 
         digitalWrite(pin, HIGH);
    }
  }
  void checkRepository(){
          HTTPClient http;
          http.begin(REPOSITORY_HOST);
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          http.POST("id=" + getID() + "&version=" + DEV_VERSION + "&appkey=" + DHEX);
          String response = http.getString();
          http.end();
          CheckRemoteFeedback(response);  
 }
 void CheckRemoteFeedback(String json){
     StaticJsonBuffer<200> jsonBuffer;
     JsonObject& jobj = jsonBuffer.parseObject(json);
     if (!jobj.success()){
         Serial.println("parseObject() failed on CheckRemoteFeedback");
         return;
         }
         //check server answer here. We are here only if json has been parsed successfuly
         String devise_id = jobj["devise_id"];
         String portion = jobj["portion"];
         if(devise_id[0] != '\0'){
          doFeed(portion);
         } 
 }
String getID(){
  String str = WiFi.macAddress();
  String dev_id;
  for(int i=0; i <17; i++){
      if(str[i]!=':'){ dev_id += str[i]; }
      }
      return dev_id;
}
String getHex(String did) {
  String doubledid = did + did;
  builder_md5.begin();
  builder_md5.add(String(doubledid));
  builder_md5.calculate();
  return builder_md5.toString();
}
void batteryCheck(){
  float vdd = ESP.getVcc();
  if( vdd < VOLTAGE_THRESHOLD ){
   Serial.print("Curent voltage: "); Serial.println(String(vdd));   
  }
}
void checkConfigs(){
         SPIFFS.begin();
       if (!SPIFFS.open("/formated.txt", "r")){
             SPIFFS.format();
             File format = SPIFFS.open("/formated.txt", "w+");
             if (format) { 
                     format.close();
                     Serial.println("SPIFFS has been formated");
                     File configFile = SPIFFS.open("/config.txt", "w+");
                     if (configFile)  {Serial.println("config has been created"); }
                         }
            }
}
void setConfigParams(String repository_host, String repository_interval){
      File configFile = SPIFFS.open("/config.txt", "w+");
       if (configFile) {
       StaticJsonBuffer<200> jsonBuffer;
       JsonObject& data = jsonBuffer.createObject();
          data["repository_host"] = repository_host;
          data["repository_interval"] = repository_interval;
       data.printTo(configFile);
       configFile.close();
       }
}
String getConfigParam(String name){
       StaticJsonBuffer<200> jsonBuffer; 
       SPIFFS.begin();
       File configFile = SPIFFS.open("/config.txt", "r");
     if (configFile) {
          String line = configFile.readStringUntil('\n');
          configFile.close();
          JsonObject& jdata = jsonBuffer.parseObject(line);
          String paramValue = jdata[name];
          return paramValue; 
          
     }else{
          return "";
     }
}
void setSchedule(String jsonString){
      File configFile = SPIFFS.open("/schedule.txt", "w+");
       if (configFile) {
       configFile.print(jsonString);
       configFile.close();
       }
}
String getSchedule(){
       SPIFFS.begin();
       File configFile = SPIFFS.open("/schedule.txt", "r");
     if (configFile) {
          String line = configFile.readStringUntil('\n');
          configFile.close();
          return line; 
          
     }else{
          return "";
     }
}
void checkShedule(){
       //json format : {"feed1":{"hour":"10", "minute":"10", "portion":"2"}, "feed2":{"hour":"10", "minute":"10", "portion":"2"}} ...feed5
       StaticJsonBuffer<1000> jsonBuffer; 
       StaticJsonBuffer<200> jsonBufferNested;
       SPIFFS.begin();
       File configFile = SPIFFS.open("/schedule.txt", "r");
     if (configFile) {
          String line = configFile.readStringUntil('\n');
          configFile.close();
          JsonObject& jdata = jsonBuffer.parseObject(line);
               if (!jdata.success()){ Serial.println("parseObject() failed on 1 lap checkShedule"); return; }
           for(JsonObject::iterator it=jdata.begin(); it!=jdata.end(); ++it) {
               if(it->value.is<JsonObject>()){
                JsonObject& ndata = it->value;
                 if (!ndata.success()){ Serial.println("parseObject() failed on 2 lap checkShedule"); return; }
                   const char* hour;
                   const char* minute;
                   const char* portion;
                   for(JsonObject::iterator it2=ndata.begin(); it2!=ndata.end(); ++it2) {
                    const char* key = it2->key;
                    const char* value = it2->value;
                    if(strcmp(key, "hour") == 0){ hour = value; }
                    if(strcmp(key, "minute") == 0){ minute = value; }
                    if(strcmp(key, "portion") == 0){ portion = value;  } 
                   }
                if(strcmp(hour, getNowHour().c_str()) == 0 && strcmp(minute, getNowMin().c_str()) == 0){
                         doFeed(portion);
                   }
               }
          }  
     }
}
void initVars(){
 DID = getID(); 
 DHEX = getHex(DID);
                            
String rhost = getConfigParam("repository_host");
  if(rhost != ""){ 
    char *cstr = new char[rhost.length() + 1];
    rhost.toCharArray(cstr, rhost.length() + 1); 
 REPOSITORY_HOST = cstr;
  }
String rint = getConfigParam("repository_interval");
  if(rint != ""){ 
 REPOSITORY_INTERVAL = rint.toInt()*60*1000;
  } 
}
void setStaticIP(String ip, String gw, String sn){
       File configFile = SPIFFS.open("/wificonfig.txt", "w+");
       if (configFile) {
       StaticJsonBuffer<200> jsonBuffer;
       JsonObject& data = jsonBuffer.createObject();
          data["ip"] = server->arg("ip");
          data["gw"] = server->arg("gw");
          data["sn"] = server->arg("sn");
       data.printTo(configFile);
       configFile.close();
       }
}
void broadcast(){
   IPAddress broadcastIp = ~WiFi.subnetMask() | WiFi.gatewayIP();
   
   if(udp.beginPacket(broadcastIp, UPDPORT) == 1){
    //Serial.print(broadcastIp);
   }
   udp.print(WiFi.localIP());
   if(udp.endPacket() == 1){
    //Serial.println(" send endPacket");
   }
   IPAddress bIp(255, 255, 255, 255);
   if(udp.beginPacket(bIp, UPDPORT) == 1){
   // Serial.print(bIp);
   }
    udp.print(WiFi.localIP());
   if(udp.endPacket() == 1){
   //Serial.println(" send endPacket");
   }   
}
void setCurrentTime(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute){
   RtcDateTime setTime = RtcDateTime(year,month,day,hour,minute,0);
   Rtc.SetDateTime(setTime); 
}
String getCurrentTime(){
   if (Rtc.IsDateTimeValid()){
    RtcDateTime currentTime = Rtc.GetDateTime(); 
    char str[15];   //declare a string as an array of chars  
    sprintf(str, "%d/%d/%d %d:%d:%d",     //%d allows to print an integer to the string
                currentTime.Day(),               
                currentTime.Month(),               
                currentTime.Year(),                   
                currentTime.Hour(),                
                currentTime.Minute(),             
                currentTime.Second()             
           );
           return str;
   }else{
           return "0";
   }  
}
String getNowHour(){
   if (Rtc.IsDateTimeValid()){
    RtcDateTime currentTime = Rtc.GetDateTime(); 
    char str[15];   //declare a string as an array of chars  
    sprintf(str, "%d", currentTime.Hour());
           return str;
   }else{
           return "0";
   }
   
}
String getNowMin(){
   if (Rtc.IsDateTimeValid()){
    RtcDateTime currentTime = Rtc.GetDateTime(); 
    char str[15];   //declare a string as an array of chars  
    sprintf(str, "%d", currentTime.Minute());
           return str;
   }else{
           return "0";
   }
   
}
//------------- end main functions----------------------------------------  
void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, HIGH); //turn off led  
  int start_time = millis(); // remember starttime
  servo.attach(MOTOR_PIN);
  servo.write(0); // initial position
  
  Serial.begin(115200);
  Serial.print("\n Starting at :");
  Serial.println(start_time);
  WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed
  if (WiFi.SSID()==""){
    Serial.println("We haven't got any access point credentials, so get them now");   
    initialConfig = true;
  }
  else{
    WiFiManager wifiManager;
    wifiManager.getStatCred();
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    unsigned long startedAt = millis();
    Serial.print("After waiting ");
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis()- startedAt);
    Serial.print(waited/1000);
    Serial.print(" secs in setup() connection result is ");
    Serial.println(connRes);
  }

  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
    
    checkConfigs(); //check whether config exist otherwise create
    initVars(); // set vars
    timer_remote.setInterval(REPOSITORY_INTERVAL, checkRepository);
    timer_vcc.setInterval(POWERCHECK_INTERVAL, batteryCheck);
    timer_broadcast.setInterval(BROADCAST_INTERVAL, broadcast);
    timer_schedule.setInterval(SCHEDULE_INTERVAL, checkShedule);
    
    Serial.println("Starting http server...");
    server.reset(new ESP8266WebServer(WiFi.localIP(), 80));
        server->on("/whoami", [](){ 
          server->send(200, "application/json", "{\"device_name\" : \"PetFeed\", \"version\" : \"" + DEV_VERSION + "\", \"repository_host\" : \"" + REPOSITORY_HOST + "\", \"repository_interval\" : \"" + REPOSITORY_INTERVAL + "\", \"did\" : \"" + DID + "\", \"dhex\" : \"" + DHEX + "\"}"); 
        });
        server->on("/dofeed", [](){
          doFeed(server->arg("portion")); 
          server->send(200, "text/plain", String("OK"));
        });
         server->on("/vcc", [](){
          float vdd = ESP.getVcc();
          server->send(200, "text/plain", String(vdd));
        });
         server->on("/setconfigparams", [](){
          setConfigParams(server->arg("repository_host"), server->arg("repository_interval"));
          server->send(200, "text/plain", String("OK"));
          ESP.reset();
        });
        server->on("/setstaticip", [](){
          //do verify here!!!!!!
          setStaticIP(server->arg("ip"), server->arg("gw"), server->arg("sn"));
          server->send(200, "text/plain", String("OK"));
          ESP.reset();
        });
        server->on("/gettime", [](){
          server->send(200, "text/plain", getCurrentTime());
        });
        server->on("/settime", [](){
          uint8_t year  = atoi (server->arg("year").c_str ()); 
          uint8_t month  = atoi (server->arg("month").c_str ()); 
          uint8_t day  = atoi (server->arg("day").c_str ()); 
          uint8_t hour  = atoi (server->arg("hour").c_str ()); 
          uint8_t minute  = atoi (server->arg("minute").c_str ()); 
          setCurrentTime(year, month, day, hour, minute);
          server->send(200, "text/plain", String("OK"));
        });
        server->on("/getschedule", [](){
          server->send(200, "text/plain", getSchedule());
        });
        server->on("/setschedule", [](){
          setSchedule(server->arg("schedule"));
          server->send(200, "text/plain", String("OK"));
        });
        server->begin();
        Serial.println("Custom HTTP server started");
        udp.begin(UPDPORT);
        Rtc.Begin();
        Serial.println("UPD server started");
        doblink(LED_PIN, 1, 5000);
  }
}

void loop() {
  // is configuration portal requested?
  if ((digitalRead(TRIGGER_PIN) == LOW) || (initialConfig)) {
    Serial.println("Configuration portal requested.");
     server.reset(new ESP8266WebServer(WiFi.localIP(), 80));
     
     digitalWrite(LED_BUILTIN, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode. Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //it starts an access point and goes into a blocking loop awaiting configuration
    if (!wifiManager.startConfigPortal("PetFeed", "12345678")) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yeey :)");
    }
    digitalWrite(LED_BUILTIN, HIGH); // Turn led off as we are not in configuration mode.
    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up so resetting the device allows to go back into config mode again when it reboots.
    delay(5000);
  }

  // put your main code here, to run repeatedly:
    if (WiFi.status()==WL_CONNECTED){
  server->handleClient();
  timer_remote.run();
  timer_vcc.run();
  timer_broadcast.run();
  timer_schedule.run();
    }
}
