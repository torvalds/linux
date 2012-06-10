/*
 * drivers/mmc/mmc-pm/mmc_pm_swl_n20.c
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
 * Nanoradio sdio wifi power management API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <plat/sys_config.h>

#include "mmc_pm.h"

#define nano_msg(...)    do {printk("[nano]: "__VA_ARGS__);} while(0)

static int nano_powerup = 0;
static int nano_suspend = 0;

static int nano_gpio_ctrl(char* name, int level)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* gpio_name[4] = {"swl_n20_host_wakeup", "swl_n20_shdn",
                               "swl_n20_vcc_en", "swl_n20_vdd_en"};
    
    int i = 0;
    int ret = 0;
    
    for (i=0; i<4; i++) {
        if (strcmp(name, gpio_name[i])==0)
            break;
    }
    if (i==4) {
        nano_msg("No gpio %s for nano-wifi module\n", name);
        return -1;
    }
    
    ret = gpio_write_one_pin_value(ops->pio_hdle, level, name);
    if (ret) {
        nano_msg("Failed to set gpio %s to %d !\n", name, level);
        return -1;
    }
    if (strcmp(name, "swl_n20_vdd_en")==0) {
        nano_powerup = level;
        nano_msg("Wifi Power %s !!\n", level ? "UP" : "Off");
    }
    return 0;
}

static int nano_get_io_value(char* name)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    char* hostwake = "swl_n20_host_wakeup";
    
    if (strcmp(name, hostwake)) {
        nano_msg("No gpio %s for swl-n20\n", name);
        return -1;
    }
    
    return gpio_read_one_pin_value(ops->pio_hdle, name);
}

static void nano_standby(int instadby)
{
    if (instadby) {
        if (nano_powerup) {
            nano_gpio_ctrl("swl_n20_shdn", 0);
            nano_gpio_ctrl("swl_n20_vdd_en", 0);
            nano_gpio_ctrl("swl_n20_vcc_en", 0);
            nano_suspend = 1;
        }
    } else {
        if (nano_suspend) {
            nano_gpio_ctrl("swl_n20_vcc_en", 1);
            udelay(100);
            nano_gpio_ctrl("swl_n20_shdn", 1);
            udelay(50);
            nano_gpio_ctrl("swl_n20_vdd_en", 1);
            sunximmc_rescan_card(3, 1);
            nano_suspend = 0;
        }
    }
}

void nano_wifi_gpio_init(void)
{
    struct mmc_pm_ops *ops = &mmc_card_pm_ops;
    nano_powerup = 0;
    nano_suspend = 0;
    ops->gpio_ctrl = nano_gpio_ctrl;
    ops->get_io_val = nano_get_io_value;
    ops->standby = nano_standby;
}
