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
		.wakeup	= 1,
	},
	{
		.desc	= "vol-",
		.code	= KEY_VOLUMEDOWN,
	},
	{
		.desc	= "vol+",
		.code	= KEY_VOLUMEUP,
	},
	{
		.desc	= "menu",
		.code	= EV_MENU,
	},
	{
		.desc	= "esc",
		.code	= KEY_BACK,
	},
	{
		.desc	= "home",
		.code	= KEY_HOME,
	},
	{
		.desc	= "camera",
		.code	= KEY_CAMERA,
	},
};
struct rk29_keys_platform_data rk29_keys_pdata = {
	.buttons	= key_button,
	.chn	= -1,  //chn: 0-7, if do not use ADC,set 'chn' -1
};
static int __init key_board_init(void)
{
        int i;
        struct port_config port;

        for(i = 0; i < key_val_size; i++){
                if(key_val[i] & (1<<31)){
                        key_button[i].adc_value = key_val[i] & 0xffff;
                        key_button[i].gpio = INVALID_GPIO;
                }else{
                        port = get_port_config(key_val[i]);
                        key_button[i].gpio = port.gpio;
                        key_button[i].active_low = port.io.active_low;
                }
        }
        rk29_keys_pdata.nbuttons = key_val_size;
        rk29_keys_pdata.chn = key_adc;

        return 0;
}

