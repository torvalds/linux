
/*
 * USI wm-bn-bm-01-5(bcm4329) sdio wifi power management API
 * evb gpio define
 * A10 gpio define:
 * bcm40183_pwr        = port:PH12<1><default><default><0>
 * bcm40183_wl_regon      = port:PH11<1><default><default><0>
 *          = port:PH10<1><default><default><0>
 *          = port:PH9<1><default><default><0>
 * bcm40183_bt_rst        = port:PB05<1><default><default><0>
 * bcm40183_bt_regon      = port:PI20<1><default><default><0>
 *          = port:PI21<0><default><default><0>
 */
 
#include <linux/kernel.h>
#include <linux/module.h>
#include <plat/sys_config.h>
#include <linux/delay.h>

#include "mmc_pm.h"

#define bcm_msg(...)    do {printk("[bcm40183]: "__VA_ARGS__);} while(0)
static int bcm40183_wl_on = 0;
static int bcm40183_bt_on = 0;

#define CONFIG_CHIP_ID 1125
#if CONFIG_CHIP_ID==1125
#include <linux/regulator/consumer.h>
static int bcm40183_power_onoff(int onoff)
{
	struct regulator* wifi_ldo = NULL;
	static int first = 1;
	  
#ifndef CONFIG_AW_AXP
	bcm_msg("AXP driver is disabled, pls check !!\n");
	return 0;
#endif

	bcm_msg("bcm40183_power_onoff\n");
	wifi_ldo = regulator_get(NULL, "axp20_pll");
	if (!wifi_ldo)
		bcm_msg("Get power regulator failed\n");
	if (first) {
		bcm_msg("first time\n");
		regulator_force_disable(wifi_ldo);
		first = 0;
	}
	if (onoff) {
		bcm_msg("regulator on\n");
		regulator_set_voltage(wifi_ldo, 3300000, 3300000);
		regulator_enable(wifi_ldo);
	} else {
		bcm_msg("regulator off\n");
		regulator_disable(wifi_ldo);
	}
	return 0;
}

#endif

static int bcm40183_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[3] = {"bcm40183_wl_regon", "bcm40183_bt_regon", "bcm40183_bt_rst"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<3; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==3) {
        bcm_msg("No gpio %s for BCM40183 module\n", name);
        return -1;
    }
    
    bcm_msg("Set GPIO %s to %d !\n", name, level);
    if (strcmp(name, "bcm40183_wl_regon") == 0) {
        if (level) {
            if (bcm40183_bt_on) {
                bcm_msg("BCM40183 is already powered up by bluetooth\n");
                goto change_state;
            } else {
                bcm_msg("BCM40183 is powered up by wifi\n");
                goto power_change;
            }
        } else {
            if (bcm40183_bt_on) {
                bcm_msg("BCM40183 should stay on because of bluetooth\n");
                goto change_state;
            } else {
                bcm_msg("BCM40183 is powered off by wifi\n");
                goto power_change;
            }
        }
    }
    
    if (strcmp(name, "bcm40183_bt_regon") == 0) {
        if (level) {
            if (bcm40183_wl_on) {
                bcm_msg("BCM40183 is already powered up by wifi\n");
                goto change_state;
            } else {
                bcm_msg("BCM40183 is powered up by bt\n");
                goto power_change;
            }
        } else {
            if (bcm40183_wl_on) {
                bcm_msg("BCM40183 should stay on because of wifi\n");
                goto change_state;
            } else {
                bcm_msg("BCM40183 is powered off by bt\n");
                goto power_change;
            }
        }
    }
    
gpio_state_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        bcm_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    
    return 0;
    
power_change:
    #if CONFIG_CHIP_ID==1123
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "bcm40183_pwr");
    #elif CONFIG_CHIP_ID==1125
    ret = bcm40183_power_onoff(level);
    #else
    #error "Found wrong chip id in wifi onoff\n"
    #endif
    if (ret) {
        bcm_msg("Failed to power off BCM40183 module!\n");
        return -1;
    }
    udelay(500);
    
change_state:
    if (strcmp(name, "bcm40183_wl_regon")==0)
        bcm40183_wl_on = level;
    if (strcmp(name, "bcm40183_bt_regon")==0)
        bcm40183_bt_on = level;
    bcm_msg("BCM40183 power state change: wifi %d, bt %d !!\n", bcm40183_wl_on, bcm40183_bt_on);
    goto gpio_state_change;
}

static int bcm40183_get_gpio_value(char* name)
{
    return 0;
}

static void bcm40183_power(int mode, int* updown)
{
	if (mode) {
		if (*updown) {
            bcm40183_gpio_ctrl("bcm40183_wl_regon", 1);
		} else {
            bcm40183_gpio_ctrl("bcm40183_wl_regon", 0);
		}
	} else {
        if (bcm40183_wl_on)
            *updown = 1;
        else
            *updown = 0;
		bcm_msg("sdio wifi power state: %s\n", bcm40183_wl_on ? "on" : "off");
	}
}

void bcm40183_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    bcm40183_wl_on = 0;
    bcm40183_bt_on = 0;
    ops->gpio_ctrl = bcm40183_gpio_ctrl;
    ops->get_io_val = bcm40183_get_gpio_value;
    ops->power = bcm40183_power;
}
