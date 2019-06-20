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
#include <mach/wmt_iomux.h>
#include <linux/gpio.h>

extern void wmt_detect_sdio2(void);
extern void force_remove_sdio2(void);

int platform_wifi_power_on(void)
{
	int err = 0;
	err = gpio_request(WMT_PIN_GP62_SUSGPIO1, "wifi_chip_en");
	if (err < 0) {
		printk("request gpio for rtl8188eu failed!\n");
		return err;
	}
	gpio_direction_output(WMT_PIN_GP62_SUSGPIO1, 0);/* pull sus_gpio1 to 0 to open vcc_wifi. */
	printk("power on rtl8189.\n");
	msleep(500);
	wmt_detect_sdio2();
	printk("[rtl8189es] %s: new card, power on.\n", __FUNCTION__);
	return err;
}

void platform_wifi_power_off(void)
{
	force_remove_sdio2();

	gpio_direction_output(WMT_PIN_GP62_SUSGPIO1, 1);/* pull sus_gpio1 to 1 to close vcc_wifi. */
	printk("power off rtl8189.\n");
	gpio_free(WMT_PIN_GP62_SUSGPIO1);
	printk("[rtl8189es] %s: remove card, power off.\n", __FUNCTION__);
}
