/*
; 6 - huawei mw269x(v1/v2) sdio wifi gpio config
;hw_mw269x_wl_pwr        = port:PH12<1><default><default><0>
;hw_mw269x_wl_enb        = port:PH11<1><default><default><0>
;hw_mw269x_wl_hostwake   = port:PH10<0><default><default><0>
;hw_mw269x_wl_wake       = port:PH09<1><default><default><0>
;hw_mw269x_bt_enb        = port:PB05<1><default><default><0>
;hw_mw269x_bt_wake       = port:PI20<1><default><default><0>
;hw_mw269x_bt_hostwake   = port:PI21<0><default><default><0>

*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/sys_config.h>

#include "mmc_pm.h"

#define SDIO_MODULE_NAME "HW-MW269"
#define hw_msg(...)    do {printk("[hw-mw269]: "__VA_ARGS__);} while(0)
static int hwmw269_wl_on = 0;
static int hwmw269_bt_on = 0;

static int hwmw269_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[5] = {"hw_mw269x_wl_pwr", "hw_mw269x_wl_enb", "hw_mw269x_wl_wake", 
                         "hw_mw269x_bt_enb", "hw_mw269x_bt_wake"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<5; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==5) {
        hw_msg("No gpio %s for %s module\n", name, SDIO_MODULE_NAME);
        return -1;
    }
    
    hw_msg("Set GPIO %s to %d !\n", name, level);
    if (strcmp(name, "hw_mw269x_wl_enb") == 0) {
        if ((level && !hwmw269_bt_on)
            || (!level && !hwmw269_bt_on)) {
            hw_msg("%s is powered %s by wifi\n", SDIO_MODULE_NAME, level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                hw_msg("%s is already on by bt\n", SDIO_MODULE_NAME);
            } else {
                hw_msg("%s should stay on because of bt\n", SDIO_MODULE_NAME);
            }
            goto state_change;
        }
    }
    if (strcmp(name, "hw_mw269x_bt_enb") == 0) {
        if ((level && !hwmw269_wl_on)
            || (!level && !hwmw269_wl_on)) {
            hw_msg("%s is powered %s by bt\n", SDIO_MODULE_NAME, level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                hw_msg("%s is already on by wifi\n", SDIO_MODULE_NAME);
            } else {
                hw_msg("%s should stay on because of wifi\n", SDIO_MODULE_NAME);
            }
            goto state_change;
        }
    }

gpio_state_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        hw_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    
    return 0;
    
power_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "hw_mw269x_wl_pwr");
    if (ret) {
        hw_msg("Failed to power off %s module!\n", SDIO_MODULE_NAME);
        return -1;
    }
    udelay(500);
    
state_change:
    if (strcmp(name, "hw_mw269x_wl_enb")==0)
        hwmw269_wl_on = level;
    if (strcmp(name, "hw_mw269x_bt_enb")==0)
        hwmw269_bt_on = level;
    hw_msg("%s power state change: wifi %d, bt %d !!\n", SDIO_MODULE_NAME, hwmw269_wl_on, hwmw269_bt_on);
    
    goto gpio_state_change;
}

static int hwmw269_get_gpio_value(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    if (strcmp(name, "hw_mw269x_wl_hostwake") || strcmp(name, "hw_mw269x_bt_hostwake")) {
        hw_msg("No gpio %s for %s\n", name, SDIO_MODULE_NAME);
        return -1;
    }
    
    return gpio_read_one_pin_value(ops->pio_hdle, name);
}

void hwmw269_power(int mode, int* updown)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    
    if (mode) {
        if (*updown) {
            hw_msg("power up module %s\n", ops->mod_name);
        } else {
            hw_msg("power down module %s\n", ops->mod_name);
        }
    } else {
        if (hwmw269_wl_on || hwmw269_bt_on)
            *updown = 1;
        else
            *updown = 0;
    }
    return;
}
void hwmw269_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    hwmw269_wl_on = 0;
    hwmw269_bt_on = 0;
    ops->gpio_ctrl = hwmw269_gpio_ctrl;
    ops->get_io_val = hwmw269_get_gpio_value;
    ops->power = hwmw269_power;
}

#undef SDIO_MODULE_NAME
