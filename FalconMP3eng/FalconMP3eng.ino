#include <Adafruit_NeoPixel.h>
#include <BluetoothSerial.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// =================== NeoPixel Config ===================
#define PIN 15
#define NUM_LEDS 128
#define BRIGHTNESS 255
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

// =================== Bluetooth ===================
BluetoothSerial SerialBT;
String btName = "Falcon_effects";

// =================== YX5300 (DFPlayer compatible) ===================
HardwareSerial mp3Serial(2);  // UART2
DFRobotDFPlayerMini mp3;

// =================== Timings ===================
#define DLOOP 20   // interval in ms
#define TFULL 8    // Ramp-Up time in s
#define TCRUIZE 15 // Cruise time in s
int nloops_tf = floor(TFULL * 1000 / DLOOP);

// =================== Variables ===================
bool debug = true;
char currentCommand = '0';
bool cruiseActive = false;
unsigned long lastUpdate = 0;
int stepCounter = 0;

// Failure mode control
unsigned long failureStart = 0;
const unsigned long failureDuration = 4000; // 4 seconds

//----------------------------------------------------------
void setup() {
  if (debug) Serial.begin(115200);

  // NeoPixel
  strip.setBrightness(BRIGHTNESS);
  strip.begin();
  strip.show();

  // Bluetooth
  SerialBT.begin(btName);
  if (debug) Serial.println("Bluetooth started: " + btName);

  // YX5300
  mp3Serial.begin(9600, SERIAL_8N1, 12, 13); // RX=12, TX=13
  if (!mp3.begin(mp3Serial)) {
    if (debug) Serial.println("Error: YX5300 not found!");
    while (true);
  }
  mp3.volume(25);  // volume (0~30)
  if (debug) Serial.println("YX5300 ready.");
}

//----------------------------------------------------------
// Color functions
void set_color(int r, int g, int b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    int r_ = random(max(0, (int)(r * 0.6)), min(255, (int)(r * 1.4)));
    int g_ = random(max(0, (int)(g * 0.6)), min(255, (int)(g * 1.4)));
    int b_ = random(max(0, (int)(b * 0.6)), min(255, (int)(b * 1.4)));
    strip.setPixelColor(i, r_, g_, b_);
  }
  strip.show();
}

void AB_color(int c, int &r, int &g, int &b) {
  const int c1 = 5, c2 = 64, c3 = 128, c4 = 192;
  if (c < c1) { r = g = b = 0; }
  else if (c < c2) { r = 0; g = 0; b = map(c, c1, c2 - 1, 10, 80); }
  else if (c < c3) { r = 0; g = map(c, c2, c3 - 1, 5, 40); b = map(c, c2, c3 - 1, 80, 160); }
  else if (c < c4) { r = map(c, c3, c4 - 1, 5, 25); g = map(c, c3, c4 - 1, 40, 90); b = map(c, c3, c4 - 1, 160, 220); }
  else { r = map(c, c4, 255, 25, 50); g = map(c, c4, 255, 90, 110); b = map(c, c4, 255, 220, 255); }
}

void set_color(int c) {
  int r, g, b;
  AB_color(c, r, g, b);
  set_color(r, g, b);
}

//----------------------------------------------------------
// Ramp-Up
void phaseRampUp_nonBlocking() {
  if (millis() - lastUpdate >= DLOOP) {
    lastUpdate = millis();
    float progress = (float)stepCounter / nloops_tf;
    int c = pow(progress, 2.2) * 255;
    set_color(c);
    stepCounter++;
    if (stepCounter > nloops_tf) {
      currentCommand = '2';
      cruiseActive = true;
      stepCounter = 0;
    }
  }
}

// Cruise
void phaseCruise_nonBlocking() {
  if (millis() - lastUpdate >= DLOOP) {
    lastUpdate = millis();
    int c = 255;
    int r, g, b;
    AB_color(c, r, g, b);
    r /= 4;
    g /= 2;
    set_color(r, g, b);
  }
}

// Fade Down
void phaseFadeDown_nonBlocking() {
  if (millis() - lastUpdate >= DLOOP) {
    lastUpdate = millis();
    float progress = (float)stepCounter / nloops_tf;
    int c = pow(progress, 2.2) * 255;
    int r, g, b;
    AB_color(c, r, g, b);
    r /= 4;
    g /= 2;
    set_color(r, g, b);
    stepCounter--;
    if (stepCounter < 0) {
      currentCommand = '0';
      cruiseActive = false;
      stepCounter = 0;
      set_color(0, 0, 0); // turn off LEDs
    }
  }
}

// Failure (intense flickering for 4s)
void phaseFailure_nonBlocking() {
  if (millis() - lastUpdate >= DLOOP) {
    lastUpdate = millis();

    for (int i = 0; i < NUM_LEDS; i++) {
      int r = random(50, 200);
      int g = random(0, 80);
      int b = random(50, 255);
      strip.setPixelColor(i, r, g, b);
    }
    strip.show();
  }

  if (millis() - failureStart >= failureDuration) {
    currentCommand = '0';
    cruiseActive = false;
    stepCounter = 0;
    set_color(0, 0, 0); // LEDs off
  }
}

//----------------------------------------------------------
void loop() {
  // Read Bluetooth commands
  if (SerialBT.available()) {
    char cmd = SerialBT.read();
    if (debug) Serial.printf("Command received: %c\n", cmd);

    switch (cmd) {
      case '1': // Ramp-Up start
        currentCommand = '1';
        cruiseActive = false;
        stepCounter = 0;
        mp3.play(1); // track 001.mp3
        break;

      case '2': // Cruise
        currentCommand = '2';
        cruiseActive = true;
        break;

      case '3': // Fade Down start
        currentCommand = '3';
        cruiseActive = false;
        stepCounter = nloops_tf;
        mp3.play(2); // track 002.mp3
        break;

      case '4': // Failure (intense flickering + sound)
        currentCommand = '4';
        cruiseActive = false;
        stepCounter = 0;
        failureStart = millis();
        mp3.play(3); // track 003.mp3
        break;

      default:
        break;
    }
  }

  // Execute current phase
  if (currentCommand == '1') {
    phaseRampUp_nonBlocking();
  }
  else if (currentCommand == '2' && cruiseActive) {
    phaseCruise_nonBlocking();
  }
  else if (currentCommand == '3') {
    phaseFadeDown_nonBlocking();
  }
  else if (currentCommand == '4') {
    phaseFailure_nonBlocking();
  }
}
