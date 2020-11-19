/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#include <drv_types.h>

#ifdef CONFIG_MMC_SUNXI_POWER_CONTROL
#ifdef CONFIG_WITS_EVB_V13
	#define SDIOID	0
#else /* !CONFIG_WITS_EVB_V13 */
	#define SDIOID (CONFIG_CHIP_ID == 1123 ? 3 : 1)
#endif /* !CONFIG_WITS_EVB_V13 */

#define SUNXI_SDIO_WIFI_NUM_RTL8189ES  10
extern void sunximmc_rescan_card(unsigned id, unsigned insert);
extern int mmc_pm_get_mod_type(void);
extern int mmc_pm_gpio_ctrl(char *name, int level);
/*
 *	rtl8189es_shdn	= port:PH09<1><default><default><0>
 *	rtl8189es_wakeup	= port:PH10<1><default><default><1>
 *	rtl8189es_vdd_en  = port:PH11<1><default><default><0>
 *	rtl8189es_vcc_en  = port:PH12<1><default><default><0>
 */

int rtl8189es_sdio_powerup(void)
{
	mmc_pm_gpio_ctrl("rtl8189es_vdd_en", 1);
	udelay(100);
	mmc_pm_gpio_ctrl("rtl8189es_vcc_en", 1);
	udelay(50);
	mmc_pm_gpio_ctrl("rtl8189es_shdn", 1);
	return 0;
}

int rtl8189es_sdio_poweroff(void)
{
	mmc_pm_gpio_ctrl("rtl8189es_shdn", 0);
	mmc_pm_gpio_ctrl("rtl8189es_vcc_en", 0);
	mmc_pm_gpio_ctrl("rtl8189es_vdd_en", 0);
	return 0;
}
#endif /* CONFIG_MMC_SUNXI_POWER_CONTROL */

/*
 * Return:
 *	0:	power on successfully
 *	others:	power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;
#ifdef CONFIG_MMC_SUNXI_POWER_CONTROL
	unsigned int mod_sel = mmc_pm_get_mod_type();
#endif /* CONFIG_MMC_SUNXI_POWER_CONTROL */


#ifdef CONFIG_MMC_SUNXI_POWER_CONTROL
	if (mod_sel == SUNXI_SDIO_WIFI_NUM_RTL8189ES) {
		rtl8189es_sdio_powerup();
		sunximmc_rescan_card(SDIOID, 1);
		printk("[rtl8189es] %s: power up, rescan card.\n", __FUNCTION__);
	} else {
		ret = -1;
		printk("[rtl8189es] %s: mod_sel = %d is incorrect.\n", __FUNCTION__, mod_sel);
	}
#endif /* CONFIG_MMC_SUNXI_POWER_CONTROL */

	return ret;
}

void platform_wifi_power_off(void)
{
#ifdef CONFIG_MMC_SUNXI_POWER_CONTROL
	sunximmc_rescan_card(SDIOID, 0);
#ifdef CONFIG_RTL8188E
	rtl8189es_sdio_poweroff();
	printk("[rtl8189es] %s: remove card, power off.\n", __FUNCTION__);
#endif /* CONFIG_RTL8188E */
#endif /* CONFIG_MMC_SUNXI_POWER_CONTROL */
}
