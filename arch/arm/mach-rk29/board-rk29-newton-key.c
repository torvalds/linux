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
		.gpio	= RK29_PIN6_PA3,  // PA0--> PA3: home -> menu
		.active_low = PRESS_LEV_LOW,
		.wakeup = 0,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.gpio	= RK29_PIN6_PA2,
		.active_low = PRESS_LEV_LOW,
		.wakeup = 0,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.gpio	= RK29_PIN6_PA1,
		.active_low = PRESS_LEV_LOW,
		.wakeup = 0,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
		.gpio	= RK29_PIN6_PA5,  //PA3 --> PA5
		.active_low = PRESS_LEV_LOW,
		.wakeup = 0,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.gpio	= RK29_PIN6_PA0, //PA5-->PA0: menu-> esc
		.active_low = PRESS_LEV_LOW,
		.wakeup = 0,
	},
	{
		.desc	= "play",
		.code	= KEY_POWER,
		.gpio	= RK29_PIN6_PA7,
		.active_low = PRESS_LEV_LOW,
		.wakeup	= 1,
	},
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

