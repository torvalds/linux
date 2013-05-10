/*
 * drivers/mmc/mmc-pm/mmc_pm_swb_b23.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


/*
 * USI wm-bn-bm-01-5(bcm4329) sdio wifi power management API
 * gpio define                                 
 * swbb23_wl_pwr           = port:PH12<1><default><default><0>
 * swbb23_wl_shdn          = port:PH09<1><default><default><0>
 * swbb23_wl_wake          = port:PB10<1><default><default><0>
 * swbb23_bt_shdn          = port:PB05<1><default><default><0>
 * swbb23_bt_wake          = port:PI20<1><default><default><0>
 * swbb23_bt_hostwake      = port:PI21<0><default><default><0>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <plat/sys_config.h>

#include "mmc_pm.h"

#define swb_msg(...)    do {printk("[swbb23]: "__VA_ARGS__);} while(0)
static int swbb23_wl_on = 0;
static int swbb23_bt_on = 0;

static int swbb23_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_cmd[5] = {"swbb23_wl_shdn", "swbb23_wl_wake", "swbb23_bt_shdn", 
                               "swbb23_bt_wake", "swbb23_bt_hostwake"};
    int i = 0;
    int ret = 0;
    
    for (i=0; i<5; i++) {
        if (strcmp(name, gpio_cmd[i])==0)
            break;
    }
    if (i==5) {
        swb_msg("No gpio %s for SWB-B23 module\n", name);
        return -1;
    }
    
//    swb_msg("Set GPIO %s to %d !\n", name, level);
    if (strcmp(name, "swbb23_wl_shdn") == 0) {
        if ((level && !swbb23_bt_on)
            || (!level && !swbb23_bt_on)) {
            swb_msg("SWB-B23 is powered %s by wifi\n", level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                swb_msg("SWB-B23 is already on by bt\n");
            } else {
                swb_msg("SWB-B23 should stay on because of bt\n");
            }
            goto state_change;
        }
    }
    if (strcmp(name, "swbb23_bt_shdn") == 0) {
        if ((level && !swbb23_wl_on)
            || (!level && !swbb23_wl_on)) {
            swb_msg("SWB-B23 is powered %s by bt\n", level ? "up" : "down");
            goto power_change;
        } else {
            if (level) {
                swb_msg("SWB-B23 is already on by wifi\n");
            } else {
                swb_msg("SWB-B23 should stay on because of wifi\n");
            }
            goto state_change;
        }
    }

gpio_state_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        swb_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    
    return 0;
    
power_change:
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, "swbb23_wl_pwr");
    if (ret) {
        swb_msg("Failed to power off SWB-B23 module!\n");
        return -1;
    }
    
state_change:
    if (strcmp(name, "swbb23_wl_shdn")==0)
        swbb23_wl_on = level;
    if (strcmp(name, "swbb23_bt_shdn")==0)
        swbb23_bt_on = level;
    swb_msg("SWB-B23 power state change: wifi %d, bt %d !!\n", swbb23_wl_on, swbb23_bt_on);
    
    goto gpio_state_change;
}

static int swbb23_get_gpio_value(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* bt_hostwake =  "swbb23_bt_hostwake";
    
    if (strcmp(name, bt_hostwake)) {
        swb_msg("No gpio %s for SWB-B23\n", name);
        return -1;
    }
    
    return gpio_read_one_pin_value(ops->pio_hdle, name);
}

void swbb23_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    swbb23_wl_on = 0;
    swbb23_bt_on = 0;
    ops->gpio_ctrl = swbb23_gpio_ctrl;
    ops->get_io_val = swbb23_get_gpio_value;
}
