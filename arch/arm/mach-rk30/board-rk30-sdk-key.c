#include <mach/gpio.h>
#include <plat/key.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {
	#if 0
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.gpio	= RK30_PIN6_PA0,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.gpio	= RK30_PIN6_PA1,
		.active_low = PRESS_LEV_LOW,
	},
	#endif
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.gpio	= RK30_PIN4_PC5,
		.active_low = PRESS_LEV_LOW,
	},
	#if 0
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.gpio	= RK30_PIN6_PA3,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "search",
		.code	= KEY_SEARCH,
		.gpio	= RK30_PIN6_PA4,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.gpio	= RK30_PIN6_PA5,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "sensor",
		.code	= KEY_CAMERA,
		.gpio	= RK30_PIN6_PA6,
		.active_low = PRESS_LEV_LOW,
	},
	#endif
	{
		.desc	= "play",
		.code	= KEY_POWER,
		.gpio	= RK30_PIN6_PA2,
		.active_low = PRESS_LEV_LOW,
		//.code_long_press = EV_ENCALL,
		.wakeup	= 1,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.adc_value	= 1,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	#if 0
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEUP,
		.adc_value	= 249,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	#endif
#ifdef CONFIG_MACH_RK3066_SDK
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.adc_value	= 163,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.adc_value	= 652,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.adc_value	= 402,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "camera",
		.code	= KEY_CAMERA,
		.adc_value	= 854,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
#else
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.adc_value	= 155,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.adc_value	= 630,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.adc_value	= 386,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "camera",
		.code	= KEY_CAMERA,
		.adc_value	= 827,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
#endif
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= 1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

