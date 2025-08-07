#include <M5Dial.h>
#include "M5UnitWeightI2C.h"

M5UnitWeightI2C weight_i2c;

//Define some screens we will have avaialble
const char* const SCREEN_OPTIONS[] = {
  "WEIGHT",
  "DISTANCE",
  "SPRING_RATE"
};
const int SCREEN_NUM = sizeof(SCREEN_OPTIONS) / sizeof(SCREEN_OPTIONS[0]);
bool SCREEN_CHANGED = false; //Use to track if the screen has changed.
String SCREEN_NOW = "WEIGHT";
String SCREEN_LAST = "WELCOME";

//Define some unit conversion numbers
const float GRAMS_PER_POUND = 453.59237;
const float MM_PER_INCH = 25.4;

// DEFINE THE DISPLAY - On the M5 DIAL it is 240x240 pixels.
const int DISPLAY_WIDTH = 240;
const int DISPLAY_HEIGHT = 240;
const int DISPLAY_CENTER_X = DISPLAY_WIDTH / 2;
const int DISPLAY_CENTER_Y = DISPLAY_HEIGHT / 2;

// DEFINE PIN USAGES AND MODULE SETTINGS - On the M5 Dial there are 2 Ports (A and B)

// PORT A: Connected to the WEIGHT I2C Sensor - Which is wired to a Load Cell
const int WEIGHT_I2C_SDA_PIN = 13;  // Port A - Yellow Wire - GPIO 13 -> SDA (Weight I2C Module)
const int WEIGHT_I2C_SCL_PIN = 15;  // Port A - White Wire - GPIO 15 -> SCL (Weight I2C Module)
const int WEIGHT_COLLECT_MS = 1000; // How many miliseconds to wait between each weight collection.

const int LOADCELL_MAX_GRAMS = 300000;              // Maximum weight rating for the loadcell in grams (300 kg)
const int LOADCELL_DEFAULT_OFFSET = 8277404;        // ADC reading at 0 grams (tare value from previous calibration)
const float LOADCELL_DEFAULT_SCALE = 8.33060;      // Gap or scale factor (ADC units per gram) from previous calibration

const int LOADCELL_CALIBRATION_WEIGHT =  80648;  // The amount of weight to use for calibration in grams (178 lbs pounds)

// PORT B: Connected to the linear potentiometer model: KPM18-125mm
const int POT_PIN = 2;       // Port B - Yellow Wire - GPIO 2 -> Middle (wiper) of the potentiometer
const int POT_MIN = 0;       // ADC minimum value
const int POT_MAX = 4095;    // ADC maximum value for 12-bit resolution (0–4095)
const int POT_LENGTH = 125;  // Potentiometer stroke length in mm
const int POT_COLLECT_MS = 1000; // How many miliseconds to wait between each pot collection.

// Now some Global Variables we can use
float WEIGHT_LAST = 0;  //The last weight in grams
float WEIGHT_NOW = 0;   //The current weight in grams

float POT_LAST = 0;  //The last measurement
float POT_NOW = 0;   //The current measurement
float POT_OFFSET = 0; //The POT reading value between 0 and 4095 where the user set the last TARE

// Define some Variables for the Rotary Encoder on the M5 Dial
long ENCODER_CURRENT = 0;
long ENCODER_LAST = 0;
const int ENCODER_COLECT_MS = 2000; //How many MS to wait between encoder collections.

//Some Time Functions
long CURRENT_TIME = 0;
long LAST_WEIGHT_READ_TIME = 0;
long LAST_POT_READ_TIME = 0;
long LAST_ENCODER_READ_TIME = 0;


void showBootScreen() {
  M5Dial.Display.setTextColor(WHITE);
  M5Dial.Display.fillScreen(BLACK);
  M5Dial.Display.setTextFont(&fonts::Orbitron_Light_32);
  M5Dial.Display.setTextDatum(middle_center);
  M5Dial.Display.drawString("SHOCK", DISPLAY_CENTER_X, DISPLAY_CENTER_Y - 20);
  M5Dial.Display.drawString("DYNO", DISPLAY_CENTER_X, DISPLAY_CENTER_Y + 20);
  M5Dial.Speaker.tone(4000, 150); delay(180);
  M5Dial.Speaker.tone(5000, 150); delay(180);
  M5Dial.Speaker.tone(6000, 150); delay(180);
  M5Dial.Speaker.tone(8000, 250); delay(280);
  M5Dial.Speaker.tone(6000, 150); delay(180);
  M5Dial.Speaker.tone(7000, 200); delay(220);
  M5Dial.Speaker.tone(7500, 300); delay(2000);
}

void calibrateScale(float knownWeight) {

    Serial.println("\n[Calibration Start]");

    // Step 1: Tare (zero)
    M5Dial.Display.clear();
    M5Dial.Display.drawString("REMOVE", DISPLAY_CENTER_X, DISPLAY_CENTER_Y - 20);
    M5Dial.Display.drawString("WEIGHT", DISPLAY_CENTER_X, DISPLAY_CENTER_Y + 20);

    delay(3000);
    weight_i2c.setOffset();  // Stores the current rawADC as offset
    int32_t offsetADC = weight_i2c.getRawADC();  // Capture it just for gap calc
    Serial.printf("Offset (tared) ADC: %d\n", offsetADC);

    M5Dial.Display.clear();
    M5Dial.Display.drawString("TARE", DISPLAY_CENTER_X, DISPLAY_CENTER_Y - 20);
    M5Dial.Display.drawString("COMPLETE", DISPLAY_CENTER_X, DISPLAY_CENTER_Y + 20);
    delay(1000);

    // Step 2: Place weight
    M5Dial.Display.clear();

    M5Dial.Display.drawString("ADD", DISPLAY_CENTER_X, DISPLAY_CENTER_Y - 30);
    M5Dial.Display.drawString(String(LOADCELL_CALIBRATION_WEIGHT), DISPLAY_CENTER_X, DISPLAY_CENTER_Y);
    M5Dial.Display.drawString("TO SCALE", DISPLAY_CENTER_X, DISPLAY_CENTER_Y + 30);

    Serial.printf("Now place %.2fg on the scale...\n", knownWeight);
    delay(10000);

    // Step 3: Calculate scale and apply
    int32_t rawADC = weight_i2c.getRawADC();
    float gap = rawADC - offsetADC;
    float gap_per_gram = gap / knownWeight;
    weight_i2c.setGapValue(gap_per_gram);

    Serial.printf("Captured gap: %ld\n", rawADC);
    Serial.printf("Delta: %.2f ADC units for %.2fg\n", gap, knownWeight);
    Serial.printf("Set gap-per-gram: %.5f\n", gap_per_gram);

    M5Dial.Display.clear();
    M5Dial.Display.setCursor(10, 40);
    M5Dial.Display.println("DONE!");
    delay(1500);
    M5Dial.Display.clear();

    Serial.println("[Calibration Complete]");
}


void showWeightScreen() {
  
  static float last_shown_weight_lbs = -1.0;  // Keeps its value between calls

  float current_weight_lbs = WEIGHT_NOW / GRAMS_PER_POUND;
  float rounded_weight = roundf(current_weight_lbs * 10) / 10.0;

  if (rounded_weight != last_shown_weight_lbs) {
    last_shown_weight_lbs = rounded_weight;

    M5Dial.Display.setTextColor(BLACK);
    M5Dial.Display.fillScreen(GREEN);
    M5Dial.Display.setTextFont(&fonts::Orbitron_Light_32);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.drawString("SCALE:", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 - 20);

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f lbs", rounded_weight);
    M5Dial.Display.drawString(buffer, M5Dial.Display.width() / 2, M5Dial.Display.height() / 2 + 20);
  }
}

void setup() {

    auto cfg = M5.config();
    M5Dial.begin(cfg, true, false);
    Serial.begin(115200);

    M5Dial.Display.setBrightness(200);
    showBootScreen();


    M5Dial.Encoder.begin();  // ✅ Prevents null reads

    Serial.begin(115200);
    delay(100);

    // Initialize I2C with Port A pins (SDA=13, SCL=15)
    Wire.begin(13, 15);  // M5Dial Port A I2C pins

    while (!weight_i2c.begin(&Wire, DEVICE_DEFAULT_ADDR, 100000U)) {
        Serial.println("weight i2c connect error");
        delay(100);
    }

    delay(3000);
    weight_i2c.setOffset();
    weight_i2c.setGapValue(LOADCELL_DEFAULT_SCALE);
    //calibrateScale(LOADCELL_CALIBRATION_WEIGHT);

}


void loop() {

  M5.update();
  CURRENT_TIME = millis();

  monitor_dial_rotation();

  collect_sensor_data();

  show_current_screen();

}

void show_current_screen(){


  if(SCREEN_CHANGED){
    //M5 Dial clear screen
  }

  if(SCREEN_NOW == "WEIGHT"){
    showWeightScreen();
  }


}


void collect_sensor_data(){


  if (CURRENT_TIME - LAST_WEIGHT_READ_TIME >= WEIGHT_COLLECT_MS) {
    WEIGHT_NOW = weight_i2c.getWeight();  // ✅ No 'float' keyword here
    Serial.printf("%.2fg\r\n", WEIGHT_NOW);

    LAST_WEIGHT_READ_TIME = CURRENT_TIME;
  }


}

void monitor_dial_rotation() {
  //We will always get the latest value;
  ENCODER_CURRENT = M5Dial.Encoder.read();

  //But we only want to interpret change after a delay 
  if (CURRENT_TIME - LAST_ENCODER_READ_TIME >= ENCODER_COLECT_MS) {
    
    long delta = ENCODER_CURRENT - ENCODER_LAST;

    if (delta != 0) {
      Serial.print("Rotary Encoder Changed: ");
      Serial.println(delta);
    }

    ENCODER_LAST = ENCODER_CURRENT;
    LAST_ENCODER_READ_TIME = CURRENT_TIME;
  }
}