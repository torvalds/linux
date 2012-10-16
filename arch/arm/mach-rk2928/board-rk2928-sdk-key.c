#include <mach/gpio.h>
#include <plat/key.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {
	{
		.desc	= "play",
		.code	= KEY_POWER,
                #if defined(CONFIG_MACH_RK2928_SDK)
                .gpio	= RK2928_PIN0_PD1,
                #else
 		.gpio   = RK2928_PIN1_PA4,
                #endif
		.active_low = PRESS_LEV_LOW,
		//.code_long_press = EV_ENCALL,
		.wakeup	= 1,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.gpio = INVALID_GPIO,
		.adc_value	= 1,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.gpio = INVALID_GPIO,
		.adc_value	= 512,
		.active_low = PRESS_LEV_LOW,
	},
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= 1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

