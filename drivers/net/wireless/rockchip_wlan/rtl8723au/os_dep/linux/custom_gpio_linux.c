/******************************************************************************
 * Customer code to add GPIO control during WLAN start/stop
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#include "osdep_service.h"
#include "drv_types.h"
#include "custom_gpio.h"

#ifdef CONFIG_PLATFORM_SPRD

//gspi func & GPIO define
#include <mach/gpio.h>//0915
#include <mach/board.h>

#if !(defined ANDROID_2X)

#ifdef CONFIG_RTL8188E
#include <mach/regulator.h>
#include <linux/regulator/consumer.h>
#endif // CONFIG_RTL8188E

#ifndef GPIO_WIFI_POWER
#define GPIO_WIFI_POWER -1
#endif // !GPIO_WIFI_POWER

#ifndef GPIO_WIFI_RESET
#define GPIO_WIFI_RESET -1
#endif // !GPIO_WIFI_RESET

#ifndef GPIO_WIFI_PWDN
#define GPIO_WIFI_PWDN -1
#endif // !GPIO_WIFI_RESET
#ifdef CONFIG_GSPI_HCI
extern unsigned int oob_irq;
#endif // CONFIG_GSPI_HCI

#ifdef CONFIG_SDIO_HCI
extern int rtw_mp_mode;
#else // !CONFIG_SDIO_HCI
#endif // !CONFIG_SDIO_HCI

int rtw_wifi_gpio_init(void)
{
#ifdef CONFIG_GSPI_HCI
	if (GPIO_WIFI_IRQ > 0) {
		gpio_request(GPIO_WIFI_IRQ, "oob_irq");
		gpio_direction_input(GPIO_WIFI_IRQ);

		oob_irq = gpio_to_irq(GPIO_WIFI_IRQ);

		DBG_8192C("%s oob_irq:%d\n", __func__, oob_irq);
	}
#endif

	if (GPIO_WIFI_RESET > 0)
		gpio_request(GPIO_WIFI_RESET , "wifi_rst");

	return 0;
}

int rtw_wifi_gpio_deinit(void)
{
#ifdef CONFIG_GSPI_HCI
	if (GPIO_WIFI_IRQ > 0) {
		gpio_free(GPIO_WIFI_IRQ);
#endif
	if (GPIO_WIFI_RESET > 0)
		gpio_free(GPIO_WIFI_RESET );

	return 0;
}

/* Customer function to control hw specific wlan gpios */
void rtw_wifi_gpio_wlan_ctrl(int onoff)
{
	switch (onoff) {
		case WLAN_PWDN_OFF:
			DBG_8192C("%s: call customer specific GPIO(%d) to set wifi power down pin to 0\n",
				__FUNCTION__, GPIO_WIFI_RESET);

			if (GPIO_WIFI_RESET > 0)
				gpio_direction_output(GPIO_WIFI_RESET , 0);
		break;

		case WLAN_PWDN_ON:
			DBG_8192C("%s: callc customer specific GPIO(%d) to set wifi power down pin to 1\n",
				__FUNCTION__, GPIO_WIFI_RESET);

			if (GPIO_WIFI_RESET > 0)
				gpio_direction_output(GPIO_WIFI_RESET , 1);
		break;

		case WLAN_POWER_OFF:
			DBG_8192C("%s: call customer specific GPIO to turn off wifi power\n",
				__FUNCTION__);
		break;
		case WLAN_POWER_ON:
			DBG_8192C("%s: call customer specific GPIO to turn on wifi power\n",
				__FUNCTION__);
		break;
	}
}
#else //ANDROID_2X
//gspi func & GPIO define
#include <mach/gpio.h>//0915
#include <mach/board.h>
#ifdef CONFIG_RTL8188E
extern int sprd_3rdparty_gpio_wifi_power;
#endif
extern int sprd_3rdparty_gpio_wifi_pwd;
#ifdef CONFIG_RTL8723A
extern int sprd_3rdparty_gpio_bt_reset;
#endif

int rtw_wifi_gpio_init(void)
{
#ifdef CONFIG_RTL8723A
	if (sprd_3rdparty_gpio_bt_reset > 0)
		gpio_direction_output(sprd_3rdparty_gpio_bt_reset, 1);
#endif

	return 0;
}

int rtw_wifi_gpio_deinit(void)
{
	return 0;
}

/* Customer function to control hw specific wlan gpios */
void rtw_wifi_gpio_wlan_ctrl(int onoff)
{
	switch (onoff) {
		case WLAN_PWDN_OFF:
			DBG_8192C("%s: call customer specific GPIO to set wifi power down pin to 0\n",
				__FUNCTION__);
			if (sprd_3rdparty_gpio_wifi_pwd > 0)
				gpio_set_value(sprd_3rdparty_gpio_wifi_pwd, 0);
		break;

		case WLAN_PWDN_ON:
			DBG_8192C("%s: callc customer specific GPIO to set wifi power down pin to 1\n",
				__FUNCTION__);
			if (sprd_3rdparty_gpio_wifi_pwd > 0)
				gpio_set_value(sprd_3rdparty_gpio_wifi_pwd, 1);
		break;

		case WLAN_POWER_OFF:
			DBG_8192C("%s: call customer specific GPIO to turn off wifi power\n",
				__FUNCTION__);
#ifdef CONFIG_RTL8188E
			if (sprd_3rdparty_gpio_wifi_power > 0)
				gpio_set_value(sprd_3rdparty_gpio_wifi_power, 0);
#endif
		break;
		case WLAN_POWER_ON:
			DBG_8192C("%s: call customer specific GPIO to turn on wifi power\n",
				__FUNCTION__);
#ifdef CONFIG_RTL8188E
			if (sprd_3rdparty_gpio_wifi_power > 0)
				gpio_set_value(sprd_3rdparty_gpio_wifi_power, 1);
#endif

		case WLAN_BT_PWDN_OFF:
			DBG_8192C("%s: call customer specific GPIO to set bt power down pin to 0\n",
				__FUNCTION__);
#ifdef CONFIG_RTL8723A
			if (sprd_3rdparty_gpio_bt_reset > 0)
				gpio_set_value(sprd_3rdparty_gpio_bt_reset, 0);
#endif
		break;

		case WLAN_BT_PWDN_ON:
			DBG_8192C("%s: callc customer specific GPIO to set bt power down pin to 1\n",
				__FUNCTION__);
#ifdef CONFIG_RTL8723A
			if (sprd_3rdparty_gpio_bt_reset > 0)
				gpio_set_value(sprd_3rdparty_gpio_bt_reset, 1);
#endif
		break;
		break;
	}
}
#endif //ANDROID_2X
#else //CONFIG_PLATFORM_SPRD
int rtw_wifi_gpio_init(void)
{
	return 0;
}

void rtw_wifi_gpio_wlan_ctrl(int onoff)
{
}
#endif //CONFIG_PLATFORM_SPRD
