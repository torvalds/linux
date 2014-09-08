#ifndef _LED_CONTROL_H
#define _LED_CONTROL_H

#define NUM_OF_LEDS				4
#define DSD_START_OFFSET			0x0200
#define EEPROM_VERSION_OFFSET			0x020E
#define EEPROM_HW_PARAM_POINTER_ADDRESS		0x0218
#define EEPROM_HW_PARAM_POINTER_ADDRRES_MAP5	0x0220
#define GPIO_SECTION_START_OFFSET		0x03
#define COMPATIBILITY_SECTION_LENGTH		42
#define COMPATIBILITY_SECTION_LENGTH_MAP5	84
#define EEPROM_MAP5_MAJORVERSION		5
#define EEPROM_MAP5_MINORVERSION		0
#define MAX_NUM_OF_BLINKS			10
#define NUM_OF_GPIO_PINS			16
#define DISABLE_GPIO_NUM			0xFF
#define EVENT_SIGNALED				1
#define MAX_FILE_NAME_BUFFER_SIZE		100

#define TURN_ON_LED(ad, GPIO, index) do {					\
		unsigned int gpio_val = GPIO;					\
		(ad->LEDInfo.LEDState[index].BitPolarity == 1) ?	\
			wrmaltWithLock(ad, BCM_GPIO_OUTPUT_SET_REG, &gpio_val, sizeof(gpio_val)) : \
			wrmaltWithLock(ad, BCM_GPIO_OUTPUT_CLR_REG, &gpio_val, sizeof(gpio_val)); \
	} while (0)

#define TURN_OFF_LED(ad, GPIO, index)  do {					\
		unsigned int gpio_val = GPIO;					\
		(ad->LEDInfo.LEDState[index].BitPolarity == 1) ?	\
			wrmaltWithLock(ad, BCM_GPIO_OUTPUT_CLR_REG, &gpio_val, sizeof(gpio_val)) : \
			wrmaltWithLock(ad, BCM_GPIO_OUTPUT_SET_REG, &gpio_val, sizeof(gpio_val)); \
	} while (0)

enum bcm_led_colors {
	RED_LED		= 1,
	BLUE_LED	= 2,
	YELLOW_LED	= 3,
	GREEN_LED	= 4
};

enum bcm_led_events {
	SHUTDOWN_EXIT		= 0x00,
	DRIVER_INIT		= 0x1,
	FW_DOWNLOAD		= 0x2,
	FW_DOWNLOAD_DONE	= 0x4,
	NO_NETWORK_ENTRY	= 0x8,
	NORMAL_OPERATION	= 0x10,
	LOWPOWER_MODE_ENTER	= 0x20,
	IDLEMODE_CONTINUE	= 0x40,
	IDLEMODE_EXIT		= 0x80,
	LED_THREAD_INACTIVE	= 0x100,  /* Makes the LED thread Inactivce. It wil be equivallent to putting the thread on hold. */
	LED_THREAD_ACTIVE	= 0x200,  /* Makes the LED Thread Active back. */
	DRIVER_HALT		= 0xff
}; /* Enumerated values of different driver states */

/*
 * Structure which stores the information of different LED types
 * and corresponding LED state information of driver states
 */
struct bcm_led_state_info {
	unsigned char LED_Type; /* specify GPIO number - use 0xFF if not used */
	unsigned char LED_On_State; /* Bits set or reset for different states */
	unsigned char LED_Blink_State; /* Bits set or reset for blinking LEDs for different states */
	unsigned char GPIO_Num;
	unsigned char BitPolarity; /* To represent whether H/W is normal polarity or reverse polarity */
};

struct bcm_led_info {
	struct bcm_led_state_info LEDState[NUM_OF_LEDS];
	bool		bIdleMode_tx_from_host; /* Variable to notify whether driver came out from idlemode due to Host or target */
	bool		bIdle_led_off;
	wait_queue_head_t	notify_led_event;
	wait_queue_head_t	idleModeSyncEvent;
	struct task_struct	*led_cntrl_threadid;
	int		led_thread_running;
	bool		bLedInitDone;
};

/* LED Thread state. */
#define BCM_LED_THREAD_DISABLED		0   /* LED Thread is not running. */
#define BCM_LED_THREAD_RUNNING_ACTIVELY	1   /* LED thread is running. */
#define BCM_LED_THREAD_RUNNING_INACTIVELY 2 /* LED thread has been put on hold */

#endif
