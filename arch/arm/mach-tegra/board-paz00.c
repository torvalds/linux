/*
 * arch/arm/mach-tegra/board-paz00.c
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Based on board-harmony.c
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/rfkill-gpio.h>
#include "board.h"

static struct rfkill_gpio_platform_data wifi_rfkill_platform_data = {
	.name	= "wifi_rfkill",
	.type	= RFKILL_TYPE_WLAN,
};

static struct platform_device wifi_rfkill_device = {
	.name	= "rfkill_gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &wifi_rfkill_platform_data,
	},
};

static struct gpiod_lookup_table wifi_gpio_lookup = {
	.dev_id = "rfkill_gpio",
	.table = {
		GPIO_LOOKUP_IDX("tegra-gpio", 25, NULL, 0, 0),
		GPIO_LOOKUP_IDX("tegra-gpio", 85, NULL, 1, 0),
		{ },
	},
};

void __init tegra_paz00_wifikill_init(void)
{
	gpiod_add_lookup_table(&wifi_gpio_lookup);
	platform_device_register(&wifi_rfkill_device);
}
