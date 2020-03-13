#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <TM1637Display.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define RGBLED_PIN (1)
#define DISPLAY_DIO_PIN (0)
#define NUM_ROWS (8)
#define MAX_TIME_WITHOUT_UPDATE_IN_MS (7200000) // 2 hours

ESP8266WebServer web_server(80);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_ROWS, RGBLED_PIN, NEO_RGB + NEO_KHZ800);

int black = pixels.Color(0,0,0);
int red = pixels.Color(02,00,00);
int green = pixels.Color(00,02,00);
int orange = pixels.Color(02,01,00);

unsigned long time_since_update = millis();
bool connected = false;

const char* host = "budgetboard";

int led_color;

typedef struct Row {
  uint16_t number;
  int color;
  boolean blink;
} Row;

Row row[NUM_ROWS];

const uint32_t all_segments_off = 0x00000000;
const uint32_t spin_segments[12] = {
  SEG_E,
  SEG_F,
  SEG_A,
  SEG_A << 8,
  SEG_A << 16,
  SEG_A << 24,
  SEG_B << 24,
  SEG_C << 24,
  SEG_D << 24,
  SEG_D << 16,
  SEG_D << 8,
  SEG_D
};

#define NUM_DISPLAY_MODES 4
String display_mode_strings[NUM_DISPLAY_MODES] = {
  "OFF",
  "NUMBERS",
  "BOUNCE",
  "SPIN"
};

typedef enum DisplayMode {
  OFF,
  NUMBERS,
  BOUNCE,
  SPIN,
} DisplayMode;

DisplayMode display_mode = OFF;
int counter = 0;

TM1637Display display[NUM_ROWS] = {
  {2,DISPLAY_DIO_PIN},
  {3,DISPLAY_DIO_PIN},
  {4,DISPLAY_DIO_PIN},
  {5,DISPLAY_DIO_PIN},
  {12,DISPLAY_DIO_PIN},
  {13,DISPLAY_DIO_PIN},
  {14,DISPLAY_DIO_PIN},
  {16,DISPLAY_DIO_PIN}
};

int match_string(String* strings, int numStrings, String& val) {
  for (int i = 0; i < numStrings; i++) {
    if (strings[i] == val) { return i; }
  }
  return -1;
}

void handle_state_post() {
  StaticJsonDocument<1024> jsonDoc;
  Serial.println("Received post!");
  String body = web_server.arg("plain");
  deserializeJson(jsonDoc, body);
  String mode = jsonDoc["mode"];
  String color = jsonDoc["color"];
  led_color = strtoul(color.c_str(), NULL, 16);
  if (led_color == 0) { led_color = red; }
  mode.toUpperCase();
  int index = match_string(display_mode_strings, NUM_DISPLAY_MODES, mode);
  if (index < 0) {
    web_server.send(400, "text/plain", "Invalid mode");
    return;
  }
  display_mode = (DisplayMode) index;

  switch (display_mode) {
    case NUMBERS:
      JsonArray array = jsonDoc["rows"];
      for (int i = 0; i < NUM_ROWS; i++) {
        JsonObject object = array[i];
        String c = object["color"];
        row[i].color = strtoul(c.c_str(), NULL, 16);
        row[i].number = object["number"];
        row[i].blink = object["blink"];
      }
      break;
  }
  
  counter = 0;
  time_since_update = millis();
  web_server.send(200, "text/plain", "OK");
}

void init_web_server() {
  web_server.on("/state", HTTP_POST, [](){
    handle_state_post();
  });
  web_server.begin();

  MDNS.begin(host);
  MDNS.addService("http", "tcp", 80);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  Serial.println("Starting up Budget Board...");

  init_web_server();

  pixels.begin();
  for (int i = 0; i < NUM_ROWS; i++) {
    display[i].setBrightness(0x01);
  }
}

void handle_off_mode() {
  if (counter == 0) {
    for (int i = 0; i < NUM_ROWS; i++) {
      display[i].setBrightness(0, false);
      display[i].setSegments((const uint8_t*) &all_segments_off);
      pixels.setPixelColor(i, black);
    }
    pixels.show();
  }
  counter = (counter + 1) % 100;
  delay(250);
}

void handle_numbers_mode() {
  for (int i = 0; i < NUM_ROWS; i++) {
    if (counter == 0) {
      display[i].setBrightness(0, true);
      display[i].showNumberDec(row[i].number);
    }
    int c = row[i].blink ? (counter % 20 == 0 ? black : row[i].color) : row[i].color; 
    pixels.setPixelColor(i, c);
  }
  pixels.show();
  counter = (counter + 1) % 100;
  delay(250);  
}

void handle_bounce_mode() {
  for (int i = 0; i < NUM_ROWS; i++) {
    if (counter == 0) {
      display[i].setBrightness(0, false);
      display[i].setSegments((const uint8_t*) &all_segments_off);
    }
    int c = (i == counter || i == (14 - counter)) ? led_color : black;
    pixels.setPixelColor(i, c);
  }
  pixels.show();
  counter = (counter + 1) % ((NUM_ROWS * 2) - 2);
  delay(100);  
}

void handle_spin_mode() {
  for (int i = 0; i < NUM_ROWS; i++) {
    if (counter == 0) {
      display[i].setBrightness(0, true);
    }
    display[i].setSegments((const uint8_t*) &spin_segments[counter]);
    pixels.setPixelColor(i, black);
  }
  pixels.show();
  counter = (counter + 1) % 12;
}

void loop() {
  if (!WiFi.isConnected()) {
    led_color = red;
    handle_bounce_mode();
    connected = false;
  } else {
    if (!connected) {
      connected = true;
      display_mode = BOUNCE;
      counter = 0;
      led_color = green;
    }
    if (millis() - time_since_update > MAX_TIME_WITHOUT_UPDATE_IN_MS) {
      display_mode = SPIN;
    }
    web_server.handleClient();
    switch (display_mode) {
      case OFF:
        handle_off_mode();
        break;
      case NUMBERS:
        handle_numbers_mode();
        break;
      case BOUNCE:
        handle_bounce_mode();
        break;
      case SPIN:
        handle_spin_mode();
        break;
    }
  }
}
