/*
 * rtl8189es sdio wifi power management API
 *
 * ; 10 - realtek rtl8189es sdio wifi gpio config
 * rtl8189es_shdn       = port:PH09<1><default><default><0>
 * rtl8189es_wakeup     = port:PH10<1><default><default><1>
 * rtl8189es_vdd_en     = port:PH11<1><default><default><0>
 * rtl8189es_vcc_en     = port:PH12<1><default><default><0>
 */
 
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <plat/sys_config.h>

#include "mmc_pm.h"

#define rtl8189es_msg(...)    do {printk("[rtl8189es]: "__VA_ARGS__);} while(0)

static int rtl8189es_powerup = 0;
static int rtl8189es_suspend = 0;

static int rtl8189es_gpio_ctrl(char* name, int level)
{
	int i = 0, ret = 0;
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	char* gpio_name[4] = {	"rtl8189es_wakeup",
							"rtl8189es_shdn",
							"rtl8189es_vcc_en",
							"rtl8189es_vdd_en"
						};

    for (i=0; i<4; i++) {
        if (strcmp(name, gpio_name[i])==0)
            break;
    }
    if (i==4) {
        rtl8189es_msg("No gpio %s for rtl8189es-wifi module\n", name);
        return -1;
    }

    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        rtl8189es_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    } else
		rtl8189es_msg("Succeed to set gpio %s to %d !\n", name, level);

    if (strcmp(name, "rtl8189es_vdd_en") == 0) {
        rtl8189es_powerup = level;
        rtl8189es_msg("rtl8189es SDIO Wifi Power %s !!\n", level ? "UP" : "Off");
    }

    return 0;
}

static int rtl8189es_get_io_value(char* name)
{
	int ret = -1;
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	
    if (strcmp(name, "rtl8189es_wakeup")) {
        rtl8189es_msg("No gpio %s for rtl8189es\n", name);
        return -1;
    }
	ret = gpio_read_one_pin_value(ops->pio_hdle, name);
	rtl8189es_msg("Succeed to get gpio %s value: %d !\n", name, ret);

	return ret;
}

static void rtl8189es_standby(int instadby)
{
    if (instadby) {
        if (rtl8189es_powerup) {
            rtl8189es_gpio_ctrl("rtl8189es_shdn", 0);
            rtl8189es_gpio_ctrl("rtl8189es_vcc_en", 0);
            rtl8189es_gpio_ctrl("rtl8189es_vdd_en", 0);
            rtl8189es_suspend = 1;
        }
    } else {
        if (rtl8189es_suspend) {
            rtl8189es_gpio_ctrl("rtl8189es_vdd_en", 1);
            udelay(100);
            rtl8189es_gpio_ctrl("rtl8189es_vcc_en", 1);
            udelay(50);
            rtl8189es_gpio_ctrl("rtl8189es_shdn", 1);
            sunximmc_rescan_card(3, 1);
            rtl8189es_suspend = 0;
        }
    }
}

static void rtl8189es_power(int mode, int* updown)
{
    if (mode) {
        if (*updown) {
			rtl8189es_gpio_ctrl("rtl8189es_vdd_en", 1);
			udelay(100);
			rtl8189es_gpio_ctrl("rtl8189es_vcc_en", 1);
			udelay(50);
			rtl8189es_gpio_ctrl("rtl8189es_shdn", 1);
        } else {
			rtl8189es_gpio_ctrl("rtl8189es_shdn", 0);
			rtl8189es_gpio_ctrl("rtl8189es_vcc_en", 0);
			rtl8189es_gpio_ctrl("rtl8189es_vdd_en", 0);
        }
    } else {
        if (rtl8189es_powerup)
            *updown = 1;
        else
            *updown = 0;
		rtl8189es_msg("sdio wifi power state: %s\n", rtl8189es_powerup ? "on" : "off");
    }
    return;
}
void rtl8189es_wifi_gpio_init(void)
{
	struct mmc_pm_ops *ops = &mmc_card_pm_ops;
	int updown = 1;

	rtl8189es_msg("exec rtl8189es_wifi_gpio_init...\n");
	rtl8189es_powerup = 0;
	rtl8189es_suspend = 0;
	ops->gpio_ctrl 	  = rtl8189es_gpio_ctrl;
	ops->get_io_val   = rtl8189es_get_io_value;
	ops->standby      = rtl8189es_standby;
	ops->power        = rtl8189es_power;
	rtl8189es_power(1, &updown);
	sunximmc_rescan_card(ops->sdio_cardid, 1);
	rtl8189es_msg("power up, rescan card.\n");
}
