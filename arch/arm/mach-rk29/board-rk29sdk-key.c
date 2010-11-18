#include <mach/key.h>
#include <mach/gpio.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {
	[0] = {
		.desc	= "vol+",
		.code	= KEY_VOLUMEDOWN,
		.gpio	= RK29_PIN0_PB0,
		.active_low = PRESS_LEV_LOW,
	},
	[1] = {
		.desc	= "vol-",
		.code	= KEY_VOLUMEUP,
		.gpio	= RK29_PIN0_PB1,
		.active_low = PRESS_LEV_LOW,
	},
	[2] = {
		.desc	= "menu",
		.code	= EV_MENU,
		.gpio	= RK29_PIN0_PB2,
		.active_low = PRESS_LEV_LOW,
	},
	[3] = {
		.desc	= "home",
		.code	= KEY_HOME,
		.code_long_press = KEY_F4,
		.gpio	= RK29_PIN0_PB3,
		.active_low = PRESS_LEV_LOW,
	},
	[4] = {
		.desc	= "back",
		.code	= KEY_BACK,
		.gpio	= RK29_PIN0_PB4,
		.active_low = PRESS_LEV_LOW,
	},
	[5] = {
		.desc	= "esc",
		.code	= KEY_ESC,
		.gpio	= RK29_PIN0_PB5,
		.active_low = PRESS_LEV_LOW,
	},
#if 0
	[6] = {
		.desc	= "vol+",
		.code	= KEY_VOLUMEDOWN,
		.adc_value	= 95,
		.active_low = PRESS_LEV_LOW,
	},
	[7] = {
		.desc	= "vol-",
		.code	= KEY_VOLUMEUP,
		.adc_value	= 249,
		.active_low = PRESS_LEV_LOW,
	},
	[8] = {
		.desc	= "menu",
		.code	= EV_MENU,
		.adc_value	= 406,
		.active_low = PRESS_LEV_LOW,
	},
	[9] = {
		.desc	= "home",
		.code	= KEY_HOME,
		.code_long_press = KEY_F4,
		.adc_value	= 561,
		.active_low = PRESS_LEV_LOW,
	},
	[10] = {
		.desc	= "back",
		.code	= KEY_BACK,
		.adc_value	= 726,
		.active_low = PRESS_LEV_LOW,
	},
	[11] = {
		.desc	= "esc",
		.code	= KEY_ESC,
		.adc_value	= 899,
		.active_low = PRESS_LEV_LOW,
	},
#endif
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

