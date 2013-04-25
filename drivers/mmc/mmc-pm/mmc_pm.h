/*
 * drivers/mmc/mmc-pm/mmc_pm.h
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

#ifndef MMC_PM_H
#define MMC_PM_H

#define SDIO_WIFI_POWERUP   (1)
#define SDIO_WIFI_INSUSPEND (2)

struct mmc_pm_ops {
    char*   mod_name;
    u32     sdio_card_used;
    u32     sdio_cardid;
    u32     module_sel;
    u32     pio_hdle;
    int     (*gpio_ctrl)(char* name, int level);
    int     (*get_io_val)(char* name);
    void    (*standby)(int in);
    void    (*power)(int mode, int *updown);
    
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry		*proc_root;
	struct proc_dir_entry		*proc_power;
#endif
};

void mmc_pm_ops_register(struct mmc_pm_ops* ops);
void mmc_pm_ops_register(struct mmc_pm_ops* ops);

void nano_wifi_gpio_init(void);
void usi_bm01a_gpio_init(void);
void apm_6xxx_gpio_init(void);
void swbb23_gpio_init(void);
void hwmw269_gpio_init(void);
void bcm40181_wifi_gpio_init(void);
void bcm40183_gpio_init(void);
void rtl8723as_gpio_init(void);
void rtl8189es_wifi_gpio_init(void);

extern struct mmc_pm_ops mmc_card_pm_ops;
extern void sunximmc_rescan_card(unsigned id, unsigned insert);

#endif
