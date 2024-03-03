#include <Arduino.h>
#include <Adafruit_SSD1306.h>

#define ONBOARD_LED_PIN 2
#define HBRIDGE_A 13
#define HBRIDGE_B 12
#define SCK 14
#define SDA 27
#define GREEN_LED 33
#define GREEN_BTN 32
#define RED_LED 26
#define RED_BTN 25

enum state_t
{
  IDLE,
  COUNTDOWN,
  EXTENDING,
  RETRACTING,
};

#define MS 1000
#define S (MS * 1000)
#define MINS (S * 60)

// 1 millisecond
const int64_t IDLE_STATE_US = MS;
// 30 second
const int64_t COUNTDOWN_STATE_US = 20 * MINS;
// 12.5 seconds
const int64_t EXTEND_RETRACT_STATE_US = 12500 * MS;

// Delay before we register button presses after a state change (excluding IDLE)
const int64_t BTN_DISABLE_TIME_US = 500 * MS;

#define STATE_QUEUE_LEN 6
state_t state_queue[STATE_QUEUE_LEN] = {
    IDLE,
    IDLE,
    IDLE,
    IDLE,
    IDLE,
    IDLE};
int64_t next_state_transition_time_us = 0;
int64_t last_state_transition_time_us = 0;
bool is_extended = false;

void set_direction_out()
{
  digitalWrite(HBRIDGE_A, LOW);
  digitalWrite(HBRIDGE_B, HIGH);
}

void set_direction_in()
{
  digitalWrite(HBRIDGE_A, HIGH);
  digitalWrite(HBRIDGE_B, LOW);
}

void stop_motor()
{
  digitalWrite(HBRIDGE_A, LOW);
  digitalWrite(HBRIDGE_B, LOW);
}

void print_state(state_t state)
{
  switch (state)
  {
  case IDLE:
    Serial.print("IDLE");
    break;
  case COUNTDOWN:
    Serial.print("COUNTDOWN");
    break;
  case EXTENDING:
    Serial.print("EXTENDING");
    break;
  case RETRACTING:
    Serial.print("RETRACTING");
    break;
  default:
    Serial.print("UNK ");
    Serial.print(state);
    break;
  }
}

void setup()
{
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  pinMode(HBRIDGE_A, OUTPUT);
  pinMode(HBRIDGE_B, OUTPUT);
  pinMode(GREEN_BTN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_BTN, INPUT_PULLUP);
  pinMode(RED_LED, OUTPUT);

  stop_motor();
  next_state_transition_time_us = esp_timer_get_time();
  last_state_transition_time_us = next_state_transition_time_us;

  Serial.begin(115200);
}

void loop()
{
  // read inputs
  bool green_pressed = !digitalRead(GREEN_BTN);
  bool red_pressed = !digitalRead(RED_BTN);

  int64_t current_time_us = esp_timer_get_time();
  if (next_state_transition_time_us < current_time_us)
  {
    state_t prev_state = state_queue[0];
    switch (state_queue[0])
    {
    case EXTENDING:
      is_extended = true;
      break;
    case RETRACTING:
      is_extended = false;
      break;
    }

    // We need to do a state transition because the time expired
    // Shift the state queue left
    for (size_t i = 1; i < STATE_QUEUE_LEN; ++i)
    {
      state_queue[i - 1] = state_queue[i];
    }
    state_queue[STATE_QUEUE_LEN - 1] = IDLE;

    // Then, handle the state transition!
    switch (state_queue[0])
    {
    case IDLE:
      stop_motor();
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
      break;
    case COUNTDOWN:
      stop_motor();
      next_state_transition_time_us = current_time_us + COUNTDOWN_STATE_US;
      break;
    case EXTENDING:
      set_direction_out();
      next_state_transition_time_us = current_time_us + EXTEND_RETRACT_STATE_US;
      break;
    case RETRACTING:
      set_direction_in();
      next_state_transition_time_us = current_time_us + EXTEND_RETRACT_STATE_US;
      break;
    }
    last_state_transition_time_us = current_time_us;

    if (prev_state != state_queue[0])
    {
      Serial.print("State transition: ");
      print_state(prev_state);
      Serial.print(" -> ");
      print_state(state_queue[0]);
      Serial.print(" for ");
      Serial.print((next_state_transition_time_us - current_time_us) * 1.0 / S);
      Serial.println(" seconds");
    }
  }

  int64_t remaining = (next_state_transition_time_us - current_time_us);
  bool odd_second = (remaining / 1000000) % 2 == 1;

  switch (state_queue[0])
  {
  case IDLE:
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, LOW);
    break;
  case COUNTDOWN:
    if (odd_second)
    {
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
    }
    else
    {
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(RED_LED, HIGH);
    }
    break;
  case EXTENDING:
    if (odd_second)
    {
      digitalWrite(GREEN_LED, HIGH);
    }
    else
    {
      digitalWrite(GREEN_LED, LOW);
    }
    digitalWrite(RED_LED, LOW);
    break;
  case RETRACTING:
    if (odd_second)
    {
      digitalWrite(RED_LED, HIGH);
    }
    else
    {
      digitalWrite(RED_LED, LOW);
    }
    digitalWrite(GREEN_LED, LOW);
    break;
  }

  switch (state_queue[0])
  {
  case IDLE:
    if (red_pressed)
    {
      if (is_extended)
      {
        state_queue[1] = RETRACTING;
      }
      else
      {
        state_queue[1] = EXTENDING;
      }
      for (size_t i = 2; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    else if (green_pressed)
    {
      // Enter countdown queue
      if (!is_extended)
      {
        state_queue[1] = EXTENDING;
      }
      else
      {
        state_queue[1] = IDLE;
      }
      state_queue[2] = RETRACTING;
      state_queue[3] = COUNTDOWN;
      state_queue[4] = EXTENDING;
      for (size_t i = 5; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    break;
  case COUNTDOWN:
    if (current_time_us - last_state_transition_time_us < BTN_DISABLE_TIME_US)
    {
      break;
    }
    if (green_pressed && red_pressed)
    {
      // Cut the countdown short, so we retract instead.
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
    }
    break;
  case EXTENDING:
    if (current_time_us - last_state_transition_time_us < BTN_DISABLE_TIME_US)
    {
      break;
    }
    if (green_pressed)
    {
      // Immediately stop and go to IDLE
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
      for (size_t i = 1; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    else if (red_pressed)
    {
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
      state_queue[1] = RETRACTING;
      for (size_t i = 2; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    break;
  case RETRACTING:
    if (current_time_us - last_state_transition_time_us < BTN_DISABLE_TIME_US)
    {
      break;
    }
    if (red_pressed)
    {
      // Immediately stop and go to IDLE
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
      for (size_t i = 1; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    else if (green_pressed)
    {
      next_state_transition_time_us = current_time_us + IDLE_STATE_US;
      state_queue[1] = EXTENDING;
      for (size_t i = 2; i < STATE_QUEUE_LEN; ++i)
      {
        state_queue[i] = IDLE;
      }
    }
    break;
  }

  delay(1);
}
