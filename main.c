/*
 * The scheduler is used for transferring execution from the interrupt context to the main context (your main loop).
 * This ensures that all interrupt handlers are short, and that all interrupts are processed as quickly as possible.
 * This can be very beneficial for some applications.
 *
 * Conceptually the scheduler works by using a event queue. Events are scheduled by being put in the queue using app_sched_event_put().
 * This will typically be done from a event handler running in a interrupt context.
 * By calling app_sched_execute(), events will be executed and popped from the queue one by one until the queue is empty.
 * This will typically be done in the main loop. There is no concept of priorities, so events are always executed in the order they were put in the queue.
 *
 * 详见 http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v11.0.0%2Flib_scheduler.html&cp=4_0_0_3_22
 * 就是说scheduler这个东西是interupt机制，他的作用是让其他event的interupt转到main loop里
 * LED2亮说明button的inturpet发生在main loop
 * LED3亮说明timer的inturput发生在main loop
 */


#include <stdbool.h>
#include "boards.h"
#include "nrf_drv_gpiote.h"
#include "app_error.h"
#include "app_timer.h"
#include "nrf_drv_clock.h"

#include "app_scheduler.h"
#include "nordic_common.h"
#include "app_timer_appsh.h"


// Pins for LED's and buttons.
// The diodes on the DK are connected with the cathodes to the GPIO pin, so
// clearing a pin will light the LED and setting the pin will turn of the LED.
#define LED_1_PIN                       BSP_LED_0     // LED 1 on the nRF51-DK or nRF52-DK
#define LED_2_PIN                       BSP_LED_1     // LED 3 on the nRF51-DK or nRF52-DK
#define LED_3_PIN                       BSP_LED_2     // LED 3 on the nRF51-DK or nRF52-DK
#define BUTTON_1_PIN                    BSP_BUTTON_0  // Button 1 on the nRF51-DK or nRF52-DK
#define BUTTON_2_PIN                    BSP_BUTTON_1  // Button 2 on the nRF51-DK or nRF52-DK

// General application timer settings.
#define APP_TIMER_PRESCALER             16    // Value of the RTC1 PRESCALER register.
#define APP_TIMER_OP_QUEUE_SIZE         2     // Size of timer operation queues.

//parameters for scheduler initializing: APP_SCHED_INIT()
//#define SCHED_MAX_EVENT_DATA_SIZE       sizeof(nrf_drv_gpiote_pin_t)
#define SCHED_MAX_EVENT_DATA_SIZE       MAX(APP_TIMER_SCHED_EVT_SIZE, sizeof(nrf_drv_gpiote_pin_t))
#define SCHED_QUEUE_SIZE                10

volatile uint8_t haha=0;

// Application timer ID.
APP_TIMER_DEF(m_led_a_timer_id);


// Function returns true if called from main context (CPU in thread
// mode), and returns false if called from an interrupt context. This
// is used to show what the scheduler is using, but has little use in
// a real application.
bool main_context ( void )
{
    static const uint8_t ISR_NUMBER_THREAD_MODE = 0;
    uint8_t isr_number =__get_IPSR();
    if ((isr_number ) == ISR_NUMBER_THREAD_MODE)
    {
        return true;
    }
    else
    {
        return false;
    }
}


// Function for controlling LED's based on button presses.
void button_handler(nrf_drv_gpiote_pin_t pin)
{
    uint32_t err_code;

    // Handle button presses.
    switch (pin)
    {
    case BUTTON_1_PIN:
        err_code = app_timer_start(m_led_a_timer_id, APP_TIMER_TICKS(200, APP_TIMER_PRESCALER), NULL); 	// timer开始后每一个因为runout event而发生的calling都存到scheduler里了
        APP_ERROR_CHECK(err_code);
        break;
    case BUTTON_2_PIN:
        err_code = app_timer_stop(m_led_a_timer_id); //timer 被停止了，就不会有timer run out event了, led就不会闪了
        APP_ERROR_CHECK(err_code);
        break;
    default:
        break;
    }

    // Light LED 2 if running in main context and turn it off if running in an interrupt context.
    // This has no practical use in a real application, but it is useful here in the tutorial.
    if (main_context())
    {
    	//点亮led
        nrf_drv_gpiote_out_clear(LED_2_PIN);
    }
    else
    {
        nrf_drv_gpiote_out_set(LED_2_PIN);
    }
}

//后来加上的
// Button handler function to be called by the scheduler. 										这个function里的arguement不是很懂
// Scheduler event handler就长这样，因为它要作为app_sched_event_put的其中一个arguement
void button_scheduler_event_handler(void *p_event_data, uint16_t event_size)
{
    // In this case, p_event_data is a pointer to a nrf_drv_gpiote_pin_t that represents the pin number of the button pressed.
	// The size is constant, so it is ignored.
    button_handler(*((nrf_drv_gpiote_pin_t*)p_event_data)); //里面的arguement怎么和此方程定义时的arguement不一样啊？
}


// Button event handler. button一但被检测到按下，这个event就激发这个handler。
/*
 *  gpiote还是处于interupt context，but it is short (thus takes very little time)
 *  and only schedules the button_scheduler_event_handler() by putting an event into the schedulers event queue.
 *  The main work is done by the button_scheduler_event_handler() which is called by app_sched_execute() from the main loop.
 */
void gpiote_event_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) // pin的值自动知道，因为设置的input一只处于被监测状态，一旦被激发就会call这个function，input pin的值也被自动带过来
{
    // The button_handler function could be implemented here directly, but is
    // extracted to a separate function as it makes it easier to demonstrate
    // the scheduler with less modifications to the code later in the tutorial.
    //button_handler(pin);
    app_sched_event_put(&pin, sizeof(pin), button_scheduler_event_handler);//就是说把button_scheduler_event_handler这个函数放倒scheduler里等着被call
    // 其实我觉得button_scheduler_event_handler多余了，他的作用就是call button_handler，为什么不直接call button_handler呢。
    haha++;
}


// Function for configuring GPIO.
static void gpio_config()
{
    ret_code_t err_code;

    // Initialze driver.
    err_code = nrf_drv_gpiote_init();
    APP_ERROR_CHECK(err_code);

    // Configure 3 output pins for LED's.
    nrf_drv_gpiote_out_config_t out_config = GPIOTE_CONFIG_OUT_SIMPLE(false);
    err_code = nrf_drv_gpiote_out_init(LED_1_PIN, &out_config);
    APP_ERROR_CHECK(err_code);
    err_code = nrf_drv_gpiote_out_init(LED_2_PIN, &out_config);
    APP_ERROR_CHECK(err_code);
    err_code = nrf_drv_gpiote_out_init(LED_3_PIN, &out_config);
    APP_ERROR_CHECK(err_code);

    // Set output pins (this will turn off the LED's).
    nrf_drv_gpiote_out_set(LED_1_PIN);
    nrf_drv_gpiote_out_set(LED_2_PIN);
    nrf_drv_gpiote_out_set(LED_3_PIN);


    // Make a configuration for input pints. This is suitable for both pins in this example.
    nrf_drv_gpiote_in_config_t in_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(true); // detect high to low; true means high accuracy (IN_EVENT) is used
    in_config.pull = NRF_GPIO_PIN_PULLUP; //Pin pullup resistor enabled

    // Configure input pins for buttons, with separate event handlers for each button.
    err_code = nrf_drv_gpiote_in_init(BUTTON_1_PIN, &in_config, gpiote_event_handler); // 定义input时就把要激发的handler设定了，我觉得这句话就会开启gpiote event监测
    APP_ERROR_CHECK(err_code);
    err_code = nrf_drv_gpiote_in_init(BUTTON_2_PIN, &in_config, gpiote_event_handler);
    APP_ERROR_CHECK(err_code);

    // Enable input pins for buttons.
    nrf_drv_gpiote_in_event_enable(BUTTON_1_PIN, true); // Function for enabling sensing of a GPIOTE input pin.
    nrf_drv_gpiote_in_event_enable(BUTTON_2_PIN, true);
}


// Timeout handler for the repeated timer
static void timer_handler(void * p_context) // 里面的arguement也是由于此function是app_timer_create的arguement，所一要按照app_timer_create的规矩来
{
    // Toggle LED 1.
    nrf_drv_gpiote_out_toggle(LED_1_PIN);

    // Light LED 3 if running in main context and turn it off if running in an interrupt context.
    // This has no practical use in a real application, but it is useful here in the tutorial.
    if (main_context())
    {
    	//点亮led
        nrf_drv_gpiote_out_clear(LED_3_PIN);
    }
    else
    {
        nrf_drv_gpiote_out_set(LED_3_PIN);
    }
}


// Create timers
static void create_timers()
{
    uint32_t err_code;

    // Create timers
    err_code = app_timer_create(&m_led_a_timer_id,
                                APP_TIMER_MODE_REPEATED,
                                timer_handler); // timer在创建的时候就把handler确定了，然后开始监测有没有event
    APP_ERROR_CHECK(err_code);
}


// Function starting the internal LFCLK oscillator.
// This is needed by RTC1 which is used by the Application Timer
// (When SoftDevice is enabled the LFCLK is always running and this is not needed).
static void lfclk_request(void)
{
    uint32_t err_code = nrf_drv_clock_init();
    APP_ERROR_CHECK(err_code);
    nrf_drv_clock_lfclk_request(NULL);
}


// Main function.
int main(void)
{
    // Request LowFrequence clock.
    lfclk_request();

    // Configure GPIO's.
    gpio_config();

    // Initialize the Application timer Library 这是为了开启app timer的schuduler功能. 不用scheduler设置为false。
    //APP_TIMER_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, false);
    APP_TIMER_APPSH_INIT(APP_TIMER_PRESCALER, APP_TIMER_OP_QUEUE_SIZE, true); //这个app－timer自动添加到scheduler，不用像button一样需要自己编码。

    //initialize the Scheduler
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
    /*
     * EVENT_SIZE: the maximum size of events to be passed through the scheduler.
     * QUEUE_SIZE: the maximum number of entries in scheduler queue.
     */

    // Create application timer instances.
    create_timers();

    // Main loop.
    while (true)
    {
        // Wait for interrupt. or sd_app_evt_wait() if a SoftDevice was used
        __WFI();
        //下面这个function是void类型，所以括号里不用填东西. Function for executing ALL scheduled events.
        //app_sched_execute(); //你要不说这句话，event触发的handler就不会执行。这个scheduler我觉得就像一个临时存放event的地方，你自己需要时再用。
        if(haha>2){
        	app_sched_execute();
        }

    }
}
