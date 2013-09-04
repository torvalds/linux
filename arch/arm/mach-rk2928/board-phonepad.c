#if defined(CONFIG_SC6610)
#include <linux/sc6610.h>
#endif
#if defined(CONFIG_MODEM_SOUND)
#include "../../../drivers/misc/modem_sound.h"
#endif
#include "../../../drivers/headset_observe/rk_headset.h"
/****************** default paramter *********************/
enum {
        DEF_AP_MDM = -1,
        DEF_AP_HAS_ALSA = -1,
        DEF_AP_MULTI_CARD = -1,
        DEF_AP_DATA_ONLY = -1,
};
enum {
        DEF_BP_PWR = 0x000003c2,
        DEF_BP_RST = -1,
        DEF_BP_WK_AP = 0x000003c3,
        DEF_AP_WK_BP = 0x000003c4,
        DEF_MDM_ASST = 0x000003c5,
};
enum {
        DEF_HD_IO = 0x000001b4,
        DEF_HK_IO = 0x000000d1,
};
enum {
        DEF_SPKCTL_IO = 0x000003d4,
};
enum {
        DEF_RDA_I2C = 0,
};
/*************************************************************/

/* Android Parameter */
static int ap_mdm = DEF_AP_MDM;
module_param(ap_mdm, int, 0644);
static int ap_has_alsa = DEF_AP_HAS_ALSA;
module_param(ap_has_alsa, int, 0644);
static int ap_multi_card = DEF_AP_MULTI_CARD;
module_param(ap_multi_card, int, 0644);
static int ap_data_only = DEF_AP_DATA_ONLY;
module_param(ap_data_only, int, 0644);

/* sc6610 */
static int bp_pwr = DEF_BP_PWR;
module_param(bp_pwr, int, 0644);
static int bp_rst = DEF_BP_RST;
module_param(bp_rst, int, 0644);
static int bp_wk_ap = DEF_BP_WK_AP;
module_param(bp_wk_ap, int, 0644);
static int ap_wk_bp = DEF_AP_WK_BP;
module_param(ap_wk_bp, int, 0644);
static int mdm_asst = DEF_MDM_ASST;
module_param(mdm_asst, int, 0644);
int check_sc_param(void)
{
        return 0;
}
/* headset */
static int hd_io = DEF_HD_IO;
module_param(hd_io, int, 0644);
static int hk_io = DEF_HK_IO;
module_param(hk_io, int, 0644);
int check_hd_param(void)
{
        return 0;
}
/* modem sound */
static int spkctl_io = DEF_SPKCTL_IO;
module_param(spkctl_io, int, 0644);
int check_mdm_sound_param(void)
{
        return 0;
}
/* rda5990 */
static int rda_i2c = DEF_RDA_I2C;
module_param(rda_i2c, int, 0644);
int check_rda_param(void)
{
        return 0;
}

#if defined(CONFIG_SC6610)
static int sc6610_io_init(void)
{
        return 0;
}

static int sc6610_io_deinit(void)
{
        return 0;
}

struct rk29_sc6610_data rk29_sc6610_info = {
        .io_init = sc6610_io_init,
        .io_deinit = sc6610_io_deinit,
};
struct platform_device rk29_device_sc6610 = {
        .name = "SC6610",
        .id = -1,
        .dev            = {
                .platform_data = &rk29_sc6610_info,
        }
    };

static int __init sc_board_init(void)
{
        if(check_sc_param() < 0)
                return -EINVAL;
        
        rk29_sc6610_info.bp_power = get_port_config(bp_pwr).gpio;
        rk29_sc6610_info.bp_reset = get_port_config(bp_rst).gpio;
        rk29_sc6610_info.bp_wakeup_ap = get_port_config(bp_wk_ap).gpio;
        rk29_sc6610_info.ap_wakeup_bp = get_port_config(ap_wk_bp).gpio;
        rk29_sc6610_info.modem_assert = get_port_config(mdm_asst).gpio;

        return 0;
}
#else
static int __init sc_board_init(void)
{
        return 0;
}
#endif
#if defined (CONFIG_RK_HEADSET_DET) || defined (CONFIG_RK_HEADSET_IRQ_HOOK_ADC_DET)
static int rk_headset_io_init(int gpio)
{
	int ret;
	ret = gpio_request(gpio, "headset_io");
	if(ret) 
		return ret;

//	rk30_mux_api_set(iomux_name, iomux_mode);
	gpio_pull_updown(gpio, PullDisable);
	gpio_direction_input(gpio);
	mdelay(50);
	return 0;
};

static int rk_hook_io_init(int gpio)
{
	int ret;
	ret = gpio_request(gpio, "hook_io");
	if(ret) 
		return ret;

//	rk30_mux_api_set(iomux_name, iomux_mode);
	gpio_pull_updown(gpio, PullDisable);
	gpio_direction_input(gpio);
	mdelay(50);
	return 0;
};

struct rk_headset_pdata rk_headset_info = {
        .Hook_down_type = HOOK_DOWN_HIGH,
		.headset_in_type = HEADSET_IN_HIGH,
		.hook_key_code = KEY_MEDIA,
		.headset_io_init = rk_headset_io_init,
		.hook_io_init = rk_hook_io_init,
};
struct platform_device rk_device_headset = {
		.name	= "rk_headsetdet",
		.id 	= 0,
		.dev    = {
			    .platform_data = &rk_headset_info,
		}
};

static int __init hd_board_init(void)
{
        if(check_hd_param() < 0)
                return -EINVAL;

        rk_headset_info.Headset_gpio = get_port_config(hd_io).gpio;
        rk_headset_info.Hook_gpio = get_port_config(hk_io).gpio;
        return 0;
}
#else
static int __init hd_board_init(void)
{
        return 0;
}
#endif


#if defined(CONFIG_MODEM_SOUND)

struct modem_sound_data modem_sound_info = {
};

struct platform_device modem_sound_device = {
	.name = "modem_sound",
	.id = -1,
	.dev		= {
	.platform_data = &modem_sound_info,
		}
	};
static int __init mdm_sound_board_init(void)
{
        struct port_config port;
        if(check_mdm_sound_param() < 0)
                return -EINVAL;
        port = get_port_config(spkctl_io);
        modem_sound_info.spkctl_io = port.gpio;
        modem_sound_info.spkctl_active = !port.io.active_low;
        return 0;
}
#else
static int __init mdm_sound_board_init(void)
{
        return 0;
}

#endif
#ifdef CONFIG_RDA5990
#define RDA_WIFI_CORE_ADDR (0x13)
#define RDA_WIFI_RF_ADDR (0x14) //correct add is 0x14
#define RDA_BT_CORE_ADDR (0x15)
#define RDA_BT_RF_ADDR (0x16)

#define RDA_WIFI_RF_I2C_DEVNAME "rda_wifi_rf_i2c"
#define RDA_WIFI_CORE_I2C_DEVNAME "rda_wifi_core_i2c"
#define RDA_BT_RF_I2C_DEVNAME "rda_bt_rf_i2c"
#define RDA_BT_CORE_I2C_DEVNAME "rda_bt_core_i2c"
static struct i2c_board_info __initdata rda_info[] = {
        {
		.type          = RDA_WIFI_CORE_I2C_DEVNAME,
		.addr          = RDA_WIFI_CORE_ADDR,
	        .flags         = 0,

	},
	{
		.type          = RDA_WIFI_RF_I2C_DEVNAME,
		.addr          = RDA_WIFI_RF_ADDR,
	        .flags         = 0,

	},
	{
		.type          = RDA_BT_CORE_I2C_DEVNAME,
		.addr          = RDA_BT_CORE_ADDR,
	        .flags         = 0,

	},
	{
		.type          = RDA_BT_RF_I2C_DEVNAME,
		.addr          = RDA_BT_RF_ADDR,
	        .flags         = 0,

	},
};
static int __init rda_board_init(void)
{
        int ret;

        ret = check_rda_param();

        if(ret < 0)
                return ret;
        i2c_register_board_info(rda_i2c, rda_info, ARRAY_SIZE(rda_info));
        return 0;
}
#else
static int __init rda_board_init(void)
{
        return 0;
}
#endif


static struct platform_device *phonepad_devices[] __initdata = {
#if defined(CONFIG_SC6610)
        &rk29_device_sc6610,
#endif
#if defined (CONFIG_RK_HEADSET_DET) ||  defined (CONFIG_RK_HEADSET_IRQ_HOOK_ADC_DET)
	&rk_device_headset,
#endif
#if defined (CONFIG_MODEM_SOUND)
        &modem_sound_device,
#endif
};
static void __init phonepad_board_init(void)
{
        sc_board_init();
        hd_board_init();
        mdm_sound_board_init();
        rda_board_init();
        platform_add_devices(phonepad_devices, ARRAY_SIZE(phonepad_devices));
}


