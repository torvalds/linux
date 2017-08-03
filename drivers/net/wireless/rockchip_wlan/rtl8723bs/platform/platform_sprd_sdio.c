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
#include <drv_types.h>

extern void sdhci_bus_scan(void);
#ifndef ANDROID_2X
extern int sdhci_device_attached(void);
#endif

/*
 * Return:
 *	0:	power on successfully
 *	others:	power on failed
 */
int platform_wifi_power_on(void)
{
	int ret = 0;


#ifdef CONFIG_RTL8188E
	rtw_wifi_gpio_wlan_ctrl(WLAN_POWER_ON);
#endif // CONFIG_RTL8188E

	/* Pull up pwd pin, make wifi leave power down mode. */
	rtw_wifi_gpio_init();
	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_ON);

#if (MP_DRIVER == 1) && (defined(CONFIG_RTL8723A)||defined(CONFIG_RTL8723B))
	// Pull up BT reset pin.
	rtw_wifi_gpio_wlan_ctrl(WLAN_BT_PWDN_ON);
#endif
	rtw_mdelay_os(5);

	sdhci_bus_scan();
#ifdef CONFIG_RTL8723B
	//YJ,test,130305
	rtw_mdelay_os(1000);
#endif
#ifdef ANDROID_2X
	rtw_mdelay_os(200);
#else // !ANDROID_2X
	if (1) {
		int i = 0;

		for (i = 0; i <= 50; i++) {
			msleep(10);
			if (sdhci_device_attached())
				break;
			printk("%s delay times:%d\n", __func__, i);
		}
	}
#endif // !ANDROID_2X

	return ret;
}

void platform_wifi_power_off(void)
{
	/* Pull down pwd pin, make wifi enter power down mode. */
	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_OFF);
	rtw_mdelay_os(5);
	rtw_wifi_gpio_deinit();

#ifdef CONFIG_RTL8188E
	rtw_wifi_gpio_wlan_ctrl(WLAN_POWER_OFF);
#endif // CONFIG_RTL8188E

#ifdef CONFIG_WOWLAN
	if(mmc_host)
		mmc_host->pm_flags &= ~MMC_PM_KEEP_POWER;
#endif // CONFIG_WOWLAN
}
