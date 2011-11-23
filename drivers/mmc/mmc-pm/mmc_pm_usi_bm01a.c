
/*
 * USI wm-bn-bm-01-5(bcm4329) sdio wifi power management API
 * evb gpio define
 * usi_bm01a_wl_pwr        = port:PH12<1><default><default><0>
 * usi_bm01a_wlbt_regon    = port:PI11<1><default><default><0>
 * usi_bm01a_wl_rst        = port:PI10<1><default><default><0>
 * usi_bm01a_wl_wake       = port:PI12<1><default><default><0>
 * usi_bm01a_bt_rst        = port:PB05<1><default><default><0>
 * usi_bm01a_bt_wake       = port:PI20<1><default><default><0>
 * usi_bm01a_bt_hostwake   = port:PI21<0><default><default><0>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/sys_config.h>

#include "mmc_pm.h"

#define usi_msg(...)    do {printk("[usi_bm01a]: "__VA_ARGS__);} while(0)
static int usi_bm01a_wl_on = 0;
static int usi_bm01a_bt_on = 0;

static int usi_bm01a_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[6] = {"usi_bm01a_wl_regon", "usi_bm01a_bt_regon", "usi_bm01a_wl_rst", 
                               "usi_bm01a_wl_wake", "usi_bm01a_bt_rst", "usi_bm01a_bt_wake"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<6; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==6) {
        usi_msg("No gpio %s for USI-BM01A module\n", name);
        return -1;
    }
    
//    usi_msg("Set GPIO %s to %d !\n", name, level);
    if (strcmp(name, "usi_bm01a_wl_regon") == 0) {
        if (level) {
            if (usi_bm01a_bt_on) {
                usi_msg("USI-BM01A is already powered up by bluetooth\n");
                goto change_state;
            } else {
                usi_msg("USI-BM01A is powered up by wifi\n");
                goto power_change;
            }
        } else {
            if (usi_bm01a_bt_on) {
                usi_msg("USI-BM01A should stay on because of bluetooth\n");
                goto change_state;
            } else {
                usi_msg("USI-BM01A is powered off by wifi\n");
                goto power_change;
            }
        }
    }
    
    if (strcmp(name, "usi_bm01a_bt_regon") == 0) {
        if (level) {
            if (usi_bm01a_wl_on) {
                usi_msg("USI-BM01A is already powered up by wifi\n");
                goto change_state;
            } else {
                usi_msg("USI-BM01A is powered up by bt\n");
                goto power_change;
            }
        } else {
            if (usi_bm01a_wl_on) {
                usi_msg("USI-BM01A should stay on because of wifi\n");
                goto change_state;
            } else {
                usi_msg("USI-BM01A is powered off by bt\n");
                goto power_change;
            }
        }
    }
    
    
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        usi_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    
    return 0;
    
power_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "usi_bm01a_wl_pwr");
    if (ret) {
        usi_msg("Failed to power off USI-BM01A module!\n");
        return -1;
    }
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "usi_bm01a_wlbt_regon");
    if (ret) {
        usi_msg("Failed to regon off for  USI-BM01A module!\n");
        return -1;
    }
    
change_state:
    if (strcmp(name, "usi_bm01a_wl_regon")==0)
        usi_bm01a_wl_on = level;
    if (strcmp(name, "usi_bm01a_bt_regon")==0)
        usi_bm01a_bt_on = level;
    usi_msg("USI-BM01A power state change: wifi %d, bt %d !!\n", usi_bm01a_wl_on, usi_bm01a_bt_on);
    return 0;
}

static int usi_bm01a_get_gpio_value(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* bt_hostwake =  "usi_bm01a_bt_hostwake";
    
    if (strcmp(name, bt_hostwake)) {
        usi_msg("No gpio %s for USI-BM01A\n", name);
        return -1;
    }
    
    return gpio_read_one_pin_value(ops->pio_hdle, name);
}

void usi_bm01a_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    usi_bm01a_wl_on = 0;
    usi_bm01a_bt_on = 0;
    ops->gpio_ctrl = usi_bm01a_gpio_ctrl;
    ops->get_io_val = usi_bm01a_get_gpio_value;
}
