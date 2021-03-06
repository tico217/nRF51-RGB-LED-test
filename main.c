/* 
   
   nRF51-RGB-LED-test/main.c

   Controlling an RGB LED with nRF51-DK.

   Demonstrates PWM and NUS (Nordic UART Service).

   Author: Mahesh Venkitachalam
   Website: electronut.in


   Reference:

   http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk51.v9.0.0%2Findex.html

 */

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_gpiote.h"
#include "nrf_gpio.h"
#include "nrf_drv_gpiote.h"
#include "nrf51_bitfields.h"
#include "nrf_delay.h"
#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_nus.h"
#include "softdevice_handler.h"
#include "app_timer.h"
#include "app_pwm.h"
#include "app_uart.h"
#include "app_util_platform.h"
#include "boards.h"
#include "pstorage.h"
#include "pstorage_platform.h"

static ble_nus_t m_nus;                                  
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;

// Function for assert macro callback.
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
  app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

void app_error_handler(uint32_t error_code, uint32_t line_num, 
                       const uint8_t * p_file_name) 
{
  printf("Error code: %lu line num: %lu\n", error_code, line_num);
}


// Function for the GAP initialization.
static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    
    const char deviceName[] = "RGB LED";

    err_code = sd_ble_gap_device_name_set(&sec_mode,
                                          (const uint8_t *) deviceName,
                                          strlen(deviceName));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MSEC_TO_UNITS(20, UNIT_1_25_MS);
    gap_conn_params.max_conn_interval = MSEC_TO_UNITS(75, UNIT_1_25_MS);
    gap_conn_params.slave_latency     = 0;
    gap_conn_params.conn_sup_timeout  = MSEC_TO_UNITS(4000, UNIT_10_MS);

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

// These are based on default values sent by Nordic nRFToolbox app
// Modify as neeeded
#define FORWARD "FastForward"
#define REWIND "Rewind"
#define STOP "Stop"
#define PAUSE "Pause"
#define PLAY "Play"
#define START "Start"
#define END "End"

// delay in milliseconds between PWM updates
uint32_t delay = 10;
// min/max delay,increment in milliseconds
const uint32_t delayMin = 10;
const uint32_t delayMax = 250;
const uint32_t delayInc = 25;

bool enablePWM = true;
bool pausePWM = false;

// Function for handling the data from the Nordic UART Service.
static void nus_data_handler(ble_nus_t * p_nus, uint8_t * p_data, 
                             uint16_t length)
{
  if (strstr((char*)(p_data), FORWARD)) {
    if((delay + delayInc) < delayMax) {
      delay += delayInc;
    }
  }
  else if (strstr((char*)(p_data), REWIND)) {
    if((delay - delayInc) > delayMin) {
      delay -= delayInc;
    }
  }
  else if (strstr((char*)(p_data), START)) {
    delay = delayMin;
  }
  else if (strstr((char*)(p_data), END)) {
    delay = delayMax;
  }
  else if (strstr((char*)(p_data), STOP)) {
    enablePWM = false;
  }
  else if (strstr((char*)(p_data), PLAY)) {
    enablePWM = true;
  }
  else if (strstr((char*)(p_data), PAUSE)) {
    pausePWM = !pausePWM;
  }
}

// Function for initializing services that will be used by the application.
static void services_init(void)
{
    uint32_t       err_code;
    ble_nus_init_t nus_init;
    
    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;
    
    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}


// Function for handling an event from the Connection Parameters Module.
static void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
    uint32_t err_code;
    
    if(p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle, 
                                         BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}

// Function for handling errors from the Connection Parameters module.
static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


// Function for initializing the Connection Parameters module.
static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;
    
    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = APP_TIMER_TICKS(5000, 0);
    cp_init.next_conn_params_update_delay  = APP_TIMER_TICKS(30000, 0);
    cp_init.max_conn_params_update_count   = 3;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;
    
    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

// Function for handling advertising events.
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
             break;
        case BLE_ADV_EVT_IDLE:
             break;
        default:
            break;
    }
}


// Function for the Application's S110 SoftDevice event handler.
static void on_ble_evt(ble_evt_t * p_ble_evt)
{
    uint32_t                         err_code;
    
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            break;
            
        case BLE_GAP_EVT_DISCONNECTED:
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported
            err_code = 
              sd_ble_gap_sec_params_reply(m_conn_handle, 
                                          BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, 
                                          NULL, NULL);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            break;
    }
}


// Function for dispatching a S110 SoftDevice event to all modules 
// with a S110 SoftDevice event handler.
static void ble_evt_dispatch(ble_evt_t * p_ble_evt)
{
    ble_conn_params_on_ble_evt(p_ble_evt);
    ble_nus_on_ble_evt(&m_nus, p_ble_evt);
    on_ble_evt(p_ble_evt);
    ble_advertising_on_ble_evt(p_ble_evt);    
}

// Function for the S110 SoftDevice initialization.
static void ble_stack_init(void)
{
    uint32_t err_code;
    
    // Initialize SoftDevice.
    SOFTDEVICE_HANDLER_INIT(NRF_CLOCK_LFCLKSRC_XTAL_20_PPM, NULL);

    // Enable BLE stack.
    ble_enable_params_t ble_enable_params;
    memset(&ble_enable_params, 0, sizeof(ble_enable_params));

    ble_enable_params.gatts_enable_params.service_changed = 0;
    err_code = sd_ble_enable(&ble_enable_params);
    APP_ERROR_CHECK(err_code);
    
    // Subscribe for BLE events.
    err_code = softdevice_ble_evt_handler_set(ble_evt_dispatch);
    APP_ERROR_CHECK(err_code);
}

// Function for handling app_uart events.
void uart_event_handle(app_uart_evt_t * p_event)
{
    static uint8_t data_array[BLE_NUS_MAX_DATA_LEN];
    static uint8_t index = 0;
    uint32_t       err_code;

    switch (p_event->evt_type)
    {
        case APP_UART_DATA_READY:
            UNUSED_VARIABLE(app_uart_get(&data_array[index]));
            index++;

            if ((data_array[index - 1] == '\n') || 
                (index >= (BLE_NUS_MAX_DATA_LEN)))
            {
                err_code = ble_nus_string_send(&m_nus, data_array, index);
                if (err_code != NRF_ERROR_INVALID_STATE)
                {
                    APP_ERROR_CHECK(err_code);
                }
                
                index = 0;
            }
            break;

        case APP_UART_COMMUNICATION_ERROR:
            APP_ERROR_HANDLER(p_event->data.error_communication);
            break;

        case APP_UART_FIFO_ERROR:
            APP_ERROR_HANDLER(p_event->data.error_code);
            break;

        default:
            break;
    }
}

// Function for initializing the UART module.
static void uart_init(void)
{
    uint32_t                     err_code;
    const app_uart_comm_params_t comm_params =
    {
        RX_PIN_NUMBER,
        TX_PIN_NUMBER,
        RTS_PIN_NUMBER,
        CTS_PIN_NUMBER,
        APP_UART_FLOW_CONTROL_ENABLED,
        false,
        UART_BAUDRATE_BAUDRATE_Baud38400
    };

    APP_UART_FIFO_INIT( &comm_params,
                       256,
                       256,
                       uart_event_handle,
                       APP_IRQ_PRIORITY_LOW,
                       err_code);
    APP_ERROR_CHECK(err_code);
}

// Function for initializing the Advertising functionality.
static void advertising_init(void)
{
    uint32_t      err_code;
    ble_advdata_t advdata;
    ble_advdata_t scanrsp;
    ble_uuid_t m_adv_uuids[] = {{BLE_UUID_NUS_SERVICE, 
                                 BLE_UUID_TYPE_VENDOR_BEGIN}};
    
    // Build advertising data struct to pass into @ref ble_advertising_init.
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type          = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

    memset(&scanrsp, 0, sizeof(scanrsp));
    scanrsp.uuids_complete.uuid_cnt = 
      sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    scanrsp.uuids_complete.p_uuids  = m_adv_uuids;

    ble_adv_modes_config_t options = {0};
    options.ble_adv_fast_enabled  = BLE_ADV_FAST_ENABLED;
    options.ble_adv_fast_interval = 64;
    options.ble_adv_fast_timeout  = 180;

    err_code = ble_advertising_init(&advdata, &scanrsp, &options, 
                                    on_adv_evt, NULL);
    APP_ERROR_CHECK(err_code);
}


// A flag indicating PWM status.
static volatile bool ready_flag;            

// PWM callback function
void pwm_ready_callback(uint32_t pwm_id)    
{
    ready_flag = true;
}

// Application main function.
int main(void)
{
    uint32_t err_code;

    // set up timers
    APP_TIMER_INIT(0, 4, 4, false);

    // initlialize BLE
    ble_stack_init();
    gap_params_init();
    services_init();
    advertising_init();
    conn_params_init();

    err_code = ble_advertising_start(BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);

    // init GPIOTE
    err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);

    // init PPI
    err_code = nrf_drv_ppi_init();
    APP_ERROR_CHECK(err_code);

    // intialize UART
    uart_init();

    // prints to serial port
    printf("starting...\n");

    // Create the instance "PWM1" using TIMER1.
    APP_PWM_INSTANCE(PWM1,1);                   

    // RGB LED pins
    // (Common cathode)
    uint32_t pinR = 1;
    uint32_t pinG = 2;
    uint32_t pinB = 3;
   
    // 2-channel PWM, 200Hz
    app_pwm_config_t pwm1_cfg = 
      APP_PWM_DEFAULT_CONFIG_2CH(5000L, pinR, pinG);

    /* Initialize and enable PWM. */
    err_code = app_pwm_init(&PWM1,&pwm1_cfg,pwm_ready_callback);
    APP_ERROR_CHECK(err_code);
    app_pwm_enable(&PWM1);

    // Create the instance "PWM2" using TIMER2.
    APP_PWM_INSTANCE(PWM2,2);                   
 
    // 1-channel PWM, 200Hz
    app_pwm_config_t pwm2_cfg = 
      APP_PWM_DEFAULT_CONFIG_1CH(5000L, pinB);

    /* Initialize and enable PWM. */
    err_code = app_pwm_init(&PWM2,&pwm2_cfg,pwm_ready_callback);
    APP_ERROR_CHECK(err_code);
    app_pwm_enable(&PWM2);


    // Enter main loop.
    int dir = 1;
    int val = 0;

    // main loop:
    bool pwmEnabled = true;

    while(1) {

      // only if not paused 
      if (!pausePWM) {
        
        // enable disable as needed
        if(!enablePWM) {
          if(pwmEnabled) {
            app_pwm_disable(&PWM1);
            app_pwm_disable(&PWM2);

            // This is required becauase app_pwm_disable()
            // has a bug. 
            // See: 
            // https://devzone.nordicsemi.com/question/41179/how-to-stop-pwm-and-set-pin-to-clear/
            nrf_drv_gpiote_out_task_disable(pinR);
            nrf_gpio_cfg_output(pinR);
            nrf_gpio_pin_clear(pinR);
            nrf_drv_gpiote_out_task_disable(pinG);
            nrf_gpio_cfg_output(pinG);
            nrf_gpio_pin_clear(pinG);
            nrf_drv_gpiote_out_task_disable(pinB);
            nrf_gpio_cfg_output(pinB);
            nrf_gpio_pin_clear(pinB);

            pwmEnabled = false;
          }
        }
        else {
          if(!pwmEnabled) {

            // enable PWM 

            nrf_drv_gpiote_out_task_enable(pinR);
            nrf_drv_gpiote_out_task_enable(pinG);
            nrf_drv_gpiote_out_task_enable(pinB);

            app_pwm_enable(&PWM1);
            app_pwm_enable(&PWM2);
            pwmEnabled = true;
          }
        }

        if(pwmEnabled) {
          // Set the duty cycle - keep trying until PWM is ready
          while (app_pwm_channel_duty_set(&PWM1, 0, val) == NRF_ERROR_BUSY);
          while (app_pwm_channel_duty_set(&PWM1, 1, val) == NRF_ERROR_BUSY);
          while (app_pwm_channel_duty_set(&PWM2, 0, val) == NRF_ERROR_BUSY);
        }
        
        // change direction at edges
        if(val > 99) {
          dir = -1;
        }
        else if (val < 1){
          dir = 1;
        }
        // increment/decrement
        val += dir*5;
      }      
      // delay
      nrf_delay_ms(delay);
    }
}
