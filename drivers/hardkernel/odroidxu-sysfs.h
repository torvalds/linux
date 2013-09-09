//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID Board : ODROID sysfs driver (charles.park)
//  2012.01.17
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
static struct {
	int		gpio_index;		// Control Index
	int 	gpio;			// GPIO Number
	char	*name;			// GPIO Name == sysfs attr name (must)
	bool 	output;			// 1 = Output, 0 = Input
	int 	value;			// Default Value(only for output)
	int		pud;			// Pull up/down register setting : S3C_GPIO_PULL_DOWN, UP, NONE
} sControlGpios[] = {
	{	STATUS_LED_BLUE,  	EXYNOS5410_GPB2(2),  "led_blue",		1,	1,	S3C_GPIO_PULL_NONE	},
	{	STATUS_LED_GREEN,  	EXYNOS5410_GPB2(1),  "led_green",		1,	0,	S3C_GPIO_PULL_NONE	},
	{	STATUS_LED_RED,  	EXYNOS5410_GPX2(3),  "led_red",	    	1,	0,	S3C_GPIO_PULL_NONE	},
};

#define STATUS_TIMER_PEROID     1   // 1 sec

typedef	struct	status_led__t	{
    unsigned char       blink_off;  // led blink on/off
    unsigned char       period;     // blink sec
    unsigned char       on_off;     // led on/off status
    unsigned char       hold_time;  // led blink hold time
    int                 gpio;       // control gpio
	struct hrtimer      timer;		// blink timer
	
}	status_led_t;
//[*]--------------------------------------------------------------------------------------------------[*]

MODULE_DESCRIPTION("SYSFS driver for odroid-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");  
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
