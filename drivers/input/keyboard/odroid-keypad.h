//[*]--------------------------------------------------------------------------------------------------[*]
/*
 *	
 * ODROID Dev Board key-pad header file (charles.park) 
 *
 */
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef	_ODROID_KEYPAD_H_
#define	_ODROID_KEYPAD_H_

//[*]--------------------------------------------------------------------------------------------------[*]
#define DEVICE_NAME 			"odroid-keypad"

//[*]--------------------------------------------------------------------------------------------------[*]
#define	KEY_PRESS				1
#define	KEY_RELEASE				0

#define	KEYPAD_TIMER_PERIOD		100000000	// ns : ktime_set(sec, nsec)
#define	POWEROFF_CHECK_PERIOD	5			// sec : ktime_set(sec, nsec)

#if defined(CONFIG_FB_S5P_S6E8AA1)
    #define LONGKEY_CHECK_PERIOD    3
#endif	
#if defined(CONFIG_BOARD_ODROID_X)||defined(CONFIG_BOARD_ODROID_X2)||defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)||defined(CONFIG_BOARD_ODROID_Q)||defined(CONFIG_BOARD_ODROID_Q2)
    #define LED_STATUS_PERIOD       1
    #define LED_STATUS_PORT         EXYNOS4_GPC1(0)
    #define LED_STATUS_PORT_NAME    "STATUS LED"
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
typedef	struct	odroid_keypad__t	{
	
	// keypad control
	struct input_dev		*input;			// input driver
	char					phys[32];
	
	struct hrtimer			timer;			// keypad timer
	struct hrtimer			poweroff_timer;	// force power off control
#if defined(CONFIG_FB_S5P_S6E8AA1)
    char                    pause;
	struct hrtimer			long_timer;	    // long key support
	unsigned char           long_status;
#endif	

#if defined(CONFIG_BOARD_ODROID_X)||defined(CONFIG_BOARD_ODROID_X2)||defined(CONFIG_BOARD_ODROID_U)||defined(CONFIG_BOARD_ODROID_U2)||defined(CONFIG_BOARD_ODROID_Q)||(CONFIG_BOARD_ODROID_Q2)
	struct hrtimer			led_timer;	    // long key support
#endif
	
}	odroid_keypad_t;

//[*]--------------------------------------------------------------------------------------------------[*]
#endif		/* _ODROID_KEYPAD_H_*/
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
