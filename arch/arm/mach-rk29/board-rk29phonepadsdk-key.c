#include <mach/key.h>
#include <mach/gpio.h>
#include <mach/board.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.gpio	= RK29_PIN6_PA0,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.gpio	= RK29_PIN6_PA1,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.gpio	= RK29_PIN6_PA2,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.gpio	= RK29_PIN6_PA3,
		.active_low = PRESS_LEV_LOW,
	},
	//{
	//	.desc	= "search",
	//	.code	= KEY_SEARCH,
	//	.gpio	= RK29_PIN6_PA4,
	//	.active_low = PRESS_LEV_LOW,
	//},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.gpio	= RK29_PIN6_PA5,
		.active_low = PRESS_LEV_LOW,
	},
	//{
	//	.desc	= "sensor",
	//	.code	= KEY_CAMERA,
	//	.gpio	= RK29_PIN6_PA6,
	//	.active_low = PRESS_LEV_LOW,
	//},
	{
		.desc	= "play",
		.code	= KEY_POWER,
		.gpio	= RK29_PIN6_PA7,
		.active_low = PRESS_LEV_LOW,
		//.code_long_press = EV_ENCALL,
		.wakeup	= 1,
	},
#if 0
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEDOWN,
		.adc_value	= 95,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEUP,
		.adc_value	= 249,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "menu",
		.code	= EV_MENU,
		.adc_value	= 406,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.code_long_press = KEY_F4,
		.adc_value	= 561,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_ESC,
		.adc_value	= 726,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "adkey6",
		.code	= KEY_BACK,
		.code_long_press = EV_ENCALL,
		.adc_value	= 899,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
#endif
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

