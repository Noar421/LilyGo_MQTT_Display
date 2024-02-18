#include <TFT_eSPI.h>
#include <WiFiEspAT.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include <TimeLib.h>

#include "secret.h"

#define AT_BAUD_RATE 115200
#define PIN_TFT_BL 4
#define PIN_PWR_ON 22
#define PIN_BUTTON1 6
#define PIN_BUTTON2 7
#define PIN_RED_LED 25
#define PIN_BAT_VOLT 26

#define ESP32C3_RX_PIN 9
#define ESP32C3_TX_PIN 8

#define IWIDTH 240
#define IHEIGHT 135

String daysName[7] = {
  "Dimanche",
  "Lundi",
  "Mardi",
  "Mercredi",
  "Jeudi",
  "Vendredi",
  "Samedi"
};

String monthsName[12] = {
  "Janvier",
  "Fevrier",
  "Mars",
  "Avril",
  "Mai",
  "Juin",
  "Juillet",
  "Aout",
  "Septembre",
  "Octobre",
  "Novembre",
  "Decembre"
};

enum screens {
  MQTT,
  CLOCK,
  CONFIG
};
int dispScreen = CONFIG;

struct Screen {
  String title;
  String line1;
  String line2;
  String line3;
  String line4;
  String line5;
  String line6;
  String line7;
};


struct Screen MQTTScreen = { "", "", "", "", "", "", "", "" };


IPAddress server(192, 168, 12, 142);
WiFiClient espClient;

PubSubClient client(espClient);
int status = WL_IDLE_STATUS;


TFT_eSPI tft = TFT_eSPI();            // Invoke library
TFT_eSprite img = TFT_eSprite(&tft);  // Create Sprite object "img" with pointer to "tft" object

bool buttonPressed = false;
const int8_t TIME_ZONE = 1;  // UTC + 2

unsigned long prevMillis = 0;
time_t mqttAutoDisp = 0;
time_t try_reconnect_time = 0;



//**************************
// Pong screen saver
//**************************
int16_t h = IHEIGHT;
int16_t w = IWIDTH;
int16_t paddle_h = 25;
int16_t paddle_w = 2;
int16_t lpaddle_x = 0;
int16_t rpaddle_x = w - paddle_w;
int16_t lpaddle_y = 0;
int16_t rpaddle_y = h - paddle_h;
int16_t lpaddle_d = 1;
int16_t rpaddle_d = -1;
int16_t lpaddle_ball_t = w - w / 4;
int16_t rpaddle_ball_t = w / 4;
int16_t target_y = 0;
int16_t ball_x = 2;
int16_t ball_y = 2;
int16_t oldball_x = 2;
int16_t oldball_y = 2;
int16_t ball_dx = 1;
int16_t ball_dy = 1;
int16_t ball_w = 4;
int16_t ball_h = 4;
int16_t dashline_h = 4;
int16_t dashline_w = 2;
int16_t dashline_n = (h - 2 * 16) / dashline_h;
int16_t dashline_x = w / 2 - 1;
int16_t dashline_y = dashline_h / 2;
//**************************


// Setup for core0
void setup() {

  // Configure buttons
  pinMode(PIN_BUTTON1, INPUT);
  pinMode(PIN_BUTTON2, INPUT);

  Serial.begin(115200);

  // TFT initialization
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);

  // WIFI over ESP initialization
  Serial2.setTX(ESP32C3_TX_PIN);
  Serial2.setRX(ESP32C3_RX_PIN);
  Serial2.begin(AT_BAUD_RATE);
  WiFi.init(Serial2);

  if (WiFi.status() == WL_NO_MODULE) {
    tft.println("Communication with WiFi module failed!");
    while (true)
      ;
  }

  // Clean the persistents settings
  WiFi.disconnect();
  WiFi.setPersistent();  // The next connection will be persistent
  WiFi.endAP();

  // Fixed IP settings
  IPAddress ip(192, 168, 12, 113);
  IPAddress gw(192, 168, 12, 1);
  IPAddress nm(255, 255, 255, 0);
  WiFi.config(ip, gw, gw, nm);

  //
  int status = WiFi.begin(ssid, pass);

  tft.println("-Waiting for connection to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    tft.print('.');
  }
  tft.println();
  IPAddress ip_real = WiFi.localIP();
  tft.print("> Connected : ");
  tft.println(ip_real);
  delay(1000);

  display_clear();

  // Configure pseudo RTC
  setSyncProvider(RTC_adjust);
  setSyncInterval(60 * 60 * 8);
  RTC_adjust();

  // Configure the MQTT broker
  client.setServer(server, 1883);
  client.setCallback(MQTT_callback);

  // Default screen
  dispScreen = CLOCK;

  initgame();
}

// Setup for core1
void setup1() {
}

// Core 0 loop :
void loop() {

  // If not connected, try to do so
  if ((now() - try_reconnect_time > 30) && !client.connected()) {
    MQTT_Broker_reconnect();
  } else {
    // MQTT client loop/polling
    client.loop();
  }

  // Buttons detection
  if (digitalRead(PIN_BUTTON1) == 0) {
    if (!buttonPressed) {
      buttonPressed = true;
      dispScreen += 1;
      if (dispScreen > 2) {
        dispScreen = 0;
      }
    }
  } else if (digitalRead(PIN_BUTTON2) == 0) {
    if (!buttonPressed) {
      buttonPressed = true;
      dispScreen -= 1;
      if (dispScreen < 0) {
        dispScreen = 2;
      }
    }
  } else {
    buttonPressed = false;
  }

  // Check if auto MQTT display need to revert
  if ((mqttAutoDisp != 0) && (now() - mqttAutoDisp > 5)) {
    dispScreen = CLOCK;
    mqttAutoDisp = 0;
  }

  // Refresh screen
  unsigned long curMillis = millis();
  if ((curMillis - prevMillis) > 5) {
    refreshScreen();
    prevMillis = curMillis;
  }
}

// Core 1 loop :
void loop1() {
}

//**********************************************
// DISPLAY Routines
//**********************************************

void refreshScreen() {

  // Create the sprite and clear background to black
  img.createSprite(IWIDTH, IHEIGHT);
  img.fillSprite(TFT_BLACK);

  // Fill the screen buffer as needed
  if (dispScreen == CONFIG) {
    drawClock();
  } else if (dispScreen == CLOCK) {
    drawPongClock();
  } else if (dispScreen == MQTT) {
    drawMQTTScreen();
  }
  // Push the screen buffer
  img.pushSprite(0, 0);

  // Delete sprite to free up the memory
  img.deleteSprite();
}

void drawPongClock() {
  pong();
}

void drawClock() {

  img.fillSprite(TFT_BLACK);
  img.setTextSize(1);           // Font size scaling is x1
  img.setTextFont(4);           // Font 4 selected
  img.setTextColor(TFT_WHITE);  // Black text, no background colour
  img.setTextWrap(false);       // Turn of wrap so we can print past end of sprite

  switch (timeStatus()) {
    case timeNotSet:
      img.drawString("Time not set", 0, 0, 4);
    case timeNeedsSync:
      img.drawString("Time need sync", 0, 0, 4);
    case timeSet:
      img.setTextDatum(TC_DATUM);
      char buffDay[2];
      sprintf(buffDay, "%02d", day());
      String sDate = daysName[weekday() - 1] + " " + (String)buffDay + " " + monthsName[month()];
      img.drawString(sDate, (int)IWIDTH / 2, 0, 4);
      char buffTime[8];
      sprintf(buffTime, "%02d:%02d:%02d", hour(), minute(), second());
      img.drawString(buffTime, (int)IWIDTH / 2, 50, 6);
      img.setTextDatum(TL_DATUM);
  }
}

void drawMQTTScreen() {
  img.fillSprite(TFT_BLACK);
  img.setTextSize(1);           // Font size scaling is x1
  img.setTextColor(TFT_WHITE);  // Black text, no background colour
  img.setTextWrap(false);       // Turn of wrap so we can print past end of sprite

  if (client.connected()) {
    int yPos = 0;
    img.drawString(MQTTScreen.title, 0, yPos, 4);
    yPos += 26;
    img.drawString(MQTTScreen.line1, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line2, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line3, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line4, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line5, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line6, 0, yPos, 2);
    yPos += 16;
    img.drawString(MQTTScreen.line7, 0, yPos, 2);
  } else {
    img.drawString("Broker not connected", 0, 0, 2);
  }
}

// MQTT message received callback
void MQTT_callback(char* topic, byte* payload, unsigned int length) {

  // Convert topic to string
  String sTopic = String(topic);
  String sPayload = String((char*)payload);
  // Check topic
  if (sTopic == "arduino/display") {
    // Deserialize
    JSONVar myObj = JSON.parse(sPayload);
    // Extract from json
    if (myObj.hasOwnProperty("title")) { MQTTScreen.title = (String)myObj["title"]; }
    if (myObj.hasOwnProperty("line1")) { MQTTScreen.line1 = (String)myObj["line1"]; }
    if (myObj.hasOwnProperty("line2")) { MQTTScreen.line2 = (String)myObj["line2"]; }
    if (myObj.hasOwnProperty("line3")) { MQTTScreen.line3 = (String)myObj["line3"]; }
    if (myObj.hasOwnProperty("line4")) { MQTTScreen.line4 = (String)myObj["line4"]; }
    if (myObj.hasOwnProperty("line5")) { MQTTScreen.line5 = (String)myObj["line5"]; }
    if (myObj.hasOwnProperty("line6")) { MQTTScreen.line6 = (String)myObj["line6"]; }
    if (myObj.hasOwnProperty("line7")) { MQTTScreen.line7 = (String)myObj["line7"]; }
  } else if (sTopic == "arduino/display/title") {
    MQTTScreen.title = sPayload;
  } else if (sTopic == "arduino/display/line1") {
    MQTTScreen.line1 = sPayload;
  } else if (sTopic == "arduino/display/line2") {
    MQTTScreen.line2 = sPayload;
  } else if (sTopic == "arduino/display/line3") {
    MQTTScreen.line3 = sPayload;
  } else if (sTopic == "arduino/display/line4") {
    MQTTScreen.line4 = sPayload;
  } else if (sTopic == "arduino/display/line5") {
    MQTTScreen.line5 = sPayload;
  } else if (sTopic == "arduino/display/line6") {
    MQTTScreen.line6 = sPayload;
  } else if (sTopic == "arduino/display/line7") {
    MQTTScreen.line7 = sPayload;
  }
  dispScreen = MQTT;
  mqttAutoDisp = now();
}

//
void display_clear() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
}

//**********************************************
// NTP Routines
//**********************************************

// Time sync function (used as callback for autosync)
time_t RTC_adjust() {
  // Try to get the time from NTP Server
  tft.println("-Get time from NTP server");
  WiFi.sntp("us.pool.ntp.org");
  while (WiFi.getTime() < SECS_YR_2000) {
    delay(1000);
    tft.print('.');
  }
  return (WiFi.getTime() + (SECS_PER_HOUR * TIME_ZONE));
}

//**********************************************
// MQTT Routines
//**********************************************

// Topics list subscription
void MQTT_sub_topics() {
  tft.println("-Subscribing to topics");
  client.subscribe("arduino");
  client.subscribe("arduino/display");
  client.subscribe("arduino/display/title");
  client.subscribe("arduino/display/line1");
  client.subscribe("arduino/display/line2");
  client.subscribe("arduino/display/line3");
  client.subscribe("arduino/display/line4");
  client.subscribe("arduino/display/line5");
  client.subscribe("arduino/display/line6");
  client.subscribe("arduino/display/line7");
  tft.println("> Done");
  delay(1000);
  display_clear();
}

//
void MQTT_Broker_reconnect() {

  // Loop until we're reconnected
  if (!client.connected()) {
    display_clear();
    tft.println("-Connection to MQTT broker");
    tft.println(server);

    // Attempt to connect, just a name to identify the client
    if (client.connect("LilyGo")) {
      tft.println("> Connected");  // Once connected, publish an announcement...
      MQTT_sub_topics();
    } else {
      tft.println("> Failed to reconnect");
      delay(1000);
      try_reconnect_time = now();
    }
  }
}

//**********************************************
// PONG Screen saver
//**********************************************

void initgame() {
  lpaddle_y = random(0, h - paddle_h);
  rpaddle_y = random(0, h - paddle_h);

  // ball is placed on the center of the left paddle
  ball_y = lpaddle_y + (paddle_h / 2);
  calc_target_y();
}

void pong() {
  // Left paddle
  if (lpaddle_d == 1) {
    img.fillRect(lpaddle_x, lpaddle_y, paddle_w, 1, TFT_BLACK);
  } else if (lpaddle_d == -1) {
    img.fillRect(lpaddle_x, lpaddle_y + paddle_h - 1, paddle_w, 1, TFT_BLACK);
  }
  lpaddle_y = lpaddle_y + lpaddle_d;
  if (ball_dx == 1) lpaddle_d = 0;
  else {
    if (lpaddle_y + paddle_h / 2 == target_y) lpaddle_d = 0;
    else if (lpaddle_y + paddle_h / 2 > target_y) lpaddle_d = -1;
    else lpaddle_d = 1;
  }
  if (lpaddle_y + paddle_h >= h && lpaddle_d == 1) lpaddle_d = 0;
  else if (lpaddle_y <= 0 && lpaddle_d == -1) lpaddle_d = 0;
  img.fillRect(lpaddle_x, lpaddle_y, paddle_w, paddle_h, TFT_WHITE);
  // Right paddle
  if (rpaddle_d == 1) {
    img.fillRect(rpaddle_x, rpaddle_y, paddle_w, 1, TFT_BLACK);
  } else if (rpaddle_d == -1) {
    img.fillRect(rpaddle_x, rpaddle_y + paddle_h - 1, paddle_w, 1, TFT_BLACK);
  }
  rpaddle_y = rpaddle_y + rpaddle_d;
  if (ball_dx == -1) rpaddle_d = 0;
  else {
    if (rpaddle_y + paddle_h / 2 == target_y) rpaddle_d = 0;
    else if (rpaddle_y + paddle_h / 2 > target_y) rpaddle_d = -1;
    else rpaddle_d = 1;
  }
  if (rpaddle_y + paddle_h >= h && rpaddle_d == 1) rpaddle_d = 0;
  else if (rpaddle_y <= 0 && rpaddle_d == -1) rpaddle_d = 0;
  img.fillRect(rpaddle_x, rpaddle_y, paddle_w, paddle_h, TFT_WHITE);
  // Midline
  if ((ball_x < dashline_x - ball_w) && (ball_x > dashline_x + dashline_w)) {
    return;
  } else {
    img.startWrite();
    // Quick way to draw a dashed line
    img.setAddrWindow(dashline_x, 20, dashline_w, h - 16);
    for (int16_t i = 0; i < dashline_n; i += 2) {
      img.pushColor(TFT_WHITE, dashline_w * dashline_h);  // push dash pixels
      img.pushColor(TFT_BLACK, dashline_w * dashline_h);  // push gap pixels
    }
    img.endWrite();
  }
  // Clock
  img.setTextColor(TFT_WHITE);  // Black text, no background colour
  img.setTextDatum(TC_DATUM);
  char buffTime[8];
  sprintf(buffTime, "%02d:%02d:%02d", hour(), minute(), second());
  img.drawString(buffTime, 120, 0, 4);
  img.setTextDatum(BC_DATUM);
  char buffDate[10];
  sprintf(buffDate, "%02d/%02d/%4d", day(), month(), year());
  img.drawString(buffDate, 120, 135, 4);
  img.setTextDatum(TL_DATUM);
  // Ball
  ball_x = ball_x + ball_dx;
  ball_y = ball_y + ball_dy;
  if (ball_dx == -1 && ball_x == paddle_w && ball_y + ball_h >= lpaddle_y && ball_y <= lpaddle_y + paddle_h) {
    ball_dx = ball_dx * -1;
    // dly = 5;  // change speed of ball after paddle contact
    calc_target_y();
  } else if (ball_dx == 1 && ball_x + ball_w == w - paddle_w && ball_y + ball_h >= rpaddle_y && ball_y <= rpaddle_y + paddle_h) {
    ball_dx = ball_dx * -1;
    // dly = 5;  // change speed of ball after paddle contact
    calc_target_y();
  }
  //  else if ((ball_dx == 1 && ball_x >= w) || (ball_dx == -1 && ball_x + ball_w < 0)) {
  //   dly = 5;
  // }
  if (ball_y > h - ball_w || ball_y < 0) {
    ball_dy = ball_dy * -1;
    ball_y += ball_dy;  // Keep in bounds
  }
  img.drawRect(oldball_x, oldball_y, ball_w, ball_h, TFT_BLACK);  // Less TFT refresh aliasing than line above for large balls
  img.fillRect(ball_x, ball_y, ball_w, ball_h, TFT_WHITE);
  oldball_x = ball_x;
  oldball_y = ball_y;
}

void calc_target_y() {
  int16_t target_x;
  int16_t reflections;
  int16_t y;

  if (ball_dx == 1) {
    target_x = w - ball_w;
  } else {
    target_x = -1 * (w - ball_w);
  }
  y = abs(target_x * (ball_dy / ball_dx) + ball_y);
  reflections = floor(y / h);
  if (reflections % 2 == 0) {
    target_y = y % h;
  } else {
    target_y = h - (y % h);
  }
}
