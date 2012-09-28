#include <mach/gpio.h>
#include <plat/key.h>

#define EV_ENCALL				KEY_F4
#define EV_MENU					KEY_F1

#define PRESS_LEV_LOW			1
#define PRESS_LEV_HIGH			0

static struct rk29_keys_button key_button[] = {

#ifdef CONFIG_MACH_RK30_PHONE_PAD_DS763
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
		.adc_value	= 180,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
		.adc_value	= 1,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
		.adc_value	= 460,
		.gpio = INVALID_GPIO,
		.active_low = PRESS_LEV_LOW,
	},

	{
		.desc	= "play",
		.code	= KEY_POWER,
		.gpio	= RK30_PIN6_PA2,
		.active_low = PRESS_LEV_LOW,
		//.code_long_press = EV_ENCALL,
		.wakeup	= 1,
	},
#endif

#ifdef CONFIG_MACH_RK30_PHONE_PAD_C8003
	 {
                .desc   = "vol+",
                .code   = KEY_VOLUMEUP,
                .adc_value      = 1,
                .gpio = INVALID_GPIO,
                .active_low = PRESS_LEV_LOW,
        },
        {
                .desc   = "vol-",
                .code   = KEY_VOLUMEDOWN,
                .gpio   = RK30_PIN4_PC5,
                .active_low = PRESS_LEV_LOW,
        },

	{
		.desc	= "play",
		.code	= KEY_POWER,
		.gpio	= RK30_PIN6_PA2,
		.active_low = PRESS_LEV_LOW,
		//.code_long_press = EV_ENCALL,
		.wakeup	= 1,
	},
#endif
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.nbuttons	= ARRAY_SIZE(key_button),
	.chn	= 1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};

