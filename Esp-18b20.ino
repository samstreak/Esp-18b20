#include "wifi.h"
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <OneWire.h>
#include <DallasTemperature.h>

//One Wire bus on pin X 
#define ONE_WIRE_BUS 4

//Use blue LED for Wifi
#define WIFI_LED 2
//Use red LED for boot indication
#define BOOT_LED 0
//use pin X for button to toggle switch between C and F reporting
#define SWITCH_SCALE 5
//seconds between temperature checks
#define SECS_BETWEEN_TEMP_CHECKS 3

// oneWire instance
OneWire oneWire(ONE_WIRE_BUS);

// Pass oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

//MQTT server IPs (different IPs based on which Wifi Network connected to
const char* mqtt_server = "192.168.0.172";
const char* mqtt_server_2 = "192.168.2.113";

//MQTT topic to publish under
const char* mqtt_sensor_name = "Temp0";

//MQTT topic for switching Temp Scale (C or F)
const char* mqtt_sensor_cfg = "Temp0CFG";

//MQTT device name for id to server
const char* mqtt_client_name = "ESP12_temp_sensor0";

volatile char TempScale={'F'};

volatile bool wLedState=HIGH;

volatile bool sentOneShot=false;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
float temp = 0;
float temp0 = 0;

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void reconnect() {
  wLedState=LOW;
  digitalWrite(WIFI_LED, wLedState);
  
  // Loop until we're reconnected
  while (!client.connected()) {
  
  Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(mqtt_client_name)) {
      Serial.println("connected");
    } else {
      wLedState=!wLedState;
      digitalWrite(WIFI_LED, wLedState);
      
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 3 seconds");
      // Wait 3 seconds before retrying
      delay(3000);
    }
  }

  client.subscribe(mqtt_sensor_cfg);
  
  wLedState=HIGH;
  digitalWrite(WIFI_LED, wLedState);
}
 
/*
 * The setup function. We only start the sensors here
 */
void setup(void)
{
 pinMode(BOOT_LED, OUTPUT);
 pinMode(SWITCH_SCALE, INPUT_PULLUP);
 digitalWrite(BOOT_LED, LOW); 
  // start serial port
  Serial.begin(115200);

  // Start up the library
  sensors.begin();
  sensors.setResolution(11);

  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }
  if (WiFi.localIP().toString().substring(0,10)=="192.168.2.") {
    client.setServer(mqtt_server_2, 1883);
  } else {
    client.setServer(mqtt_server, 1883);
  }
  client.setCallback(callback);
  //check temp scale topic
  client.subscribe(mqtt_sensor_cfg);

  digitalWrite(BOOT_LED, HIGH);
}

/*
 * Main function, get and show the temperature
 */
void loop(void)
{ 
  bool checkSwitch=digitalRead(SWITCH_SCALE);

  if (checkSwitch==LOW) {
   if (sentOneShot==false) {
    sentOneShot=true;
    
    digitalWrite(BOOT_LED, LOW); 
    if (TempScale=='F') {
     client.publish(mqtt_sensor_cfg, "C");
    } else {
     client.publish(mqtt_sensor_cfg, "F");
    }
   } else {
    digitalWrite(BOOT_LED, HIGH);
   }
  }
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > (SECS_BETWEEN_TEMP_CHECKS * 1000)) {
    lastMsg = now;
    sensors.requestTemperatures(); // Send the command to get temperatures

    if (TempScale=='F') {
     temp = sensors.getTempFByIndex(0);
    } else {
     temp = sensors.getTempCByIndex(0);
    }

   Serial.print(temp);
//   Serial.print((char)223);
   Serial.println(TempScale);

    //don't update mqtt if same temp value or invalid value
    if((temp > -60) && (temp <170) && (temp != temp0)) {
     // blink Wifi LED and send mqtt update
     digitalWrite(wLedState, LOW);   
     client.publish(mqtt_sensor_name, String(temp).c_str());
     digitalWrite(wLedState, HIGH);     
     //update t0 with current temp
     temp0 = temp;
    }
  }
}


void callback(char* topic, byte* payload, unsigned int length) {
 if (String(topic)==String(mqtt_sensor_cfg)) {
  Serial.print(topic);
  Serial.print(": ");
  Serial.write(payload[0]);
  Serial.print("; Ts:");
  Serial.write(TempScale);
  Serial.println("");
  
  if ((char)payload[0] != (char)TempScale) {
   if (length == 1) {
    if ((char)payload[0]=='C' || (char)payload[0]=='F') {
     sentOneShot=false;
     digitalWrite(BOOT_LED, LOW);
     TempScale=payload[0];
     delay(500);
     digitalWrite(BOOT_LED, HIGH);
    }  
   }
  }  
 }
}
