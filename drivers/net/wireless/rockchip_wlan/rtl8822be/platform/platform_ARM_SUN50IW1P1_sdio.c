/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
/*
 * Description:
 *	This file can be applied to following platforms:
 *	CONFIG_PLATFORM_ARM_SUN50IW1P1
 */
#include <drv_types.h>
#ifdef CONFIG_GPIO_WAKEUP
	#include <linux/gpio.h>
#endif

#ifdef CONFIG_MMC
	#if defined(CONFIG_PLATFORM_ARM_SUN50IW1P1)
		extern void sunxi_mmc_rescan_card(unsigned ids);
		extern void sunxi_wlan_set_power(int on);
		extern int sunxi_wlan_get_bus_index(void);
		extern int sunxi_wlan_get_oob_irq(void);
		extern int sunxi_wlan_get_oob_irq_flags(void);
	#endif
	#ifdef CONFIG_GPIO_WAKEUP
		extern unsigned int oob_irq;
	#endif
#endif /* CONFIG_MMC */

/*
 * Return:
 *	0:	power on successfully
 *	others: power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;

#ifdef CONFIG_MMC
	{

#if defined(CONFIG_PLATFORM_ARM_SUN50IW1P1)
		int wlan_bus_index = sunxi_wlan_get_bus_index();
		if (wlan_bus_index < 0)
			return wlan_bus_index;

		sunxi_wlan_set_power(1);
		mdelay(100);
		sunxi_mmc_rescan_card(wlan_bus_index);
#endif
		RTW_INFO("%s: power up, rescan card.\n", __FUNCTION__);

#ifdef CONFIG_GPIO_WAKEUP
#if defined(CONFIG_PLATFORM_ARM_SUN50IW1P1)
		oob_irq = sunxi_wlan_get_oob_irq();
#endif
#endif /* CONFIG_GPIO_WAKEUP */
	}
#endif /* CONFIG_MMC */

	return ret;
}

void platform_wifi_power_off(void)
{
#ifdef CONFIG_MMC
#if defined(CONFIG_PLATFORM_ARM_SUN50IW1P1)
	int wlan_bus_index = sunxi_wlan_get_bus_index();
	if (wlan_bus_index < 0)
		return;

	sunxi_mmc_rescan_card(wlan_bus_index);
	mdelay(100);
	sunxi_wlan_set_power(0);
#endif
	RTW_INFO("%s: remove card, power off.\n", __FUNCTION__);
#endif /* CONFIG_MMC */
}
