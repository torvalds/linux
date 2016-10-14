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

#include <linux/property.h>
#include <linux/gpio/machine.h>
#include <linux/platform_device.h>

#include "board.h"

static struct property_entry __initdata wifi_rfkill_prop[] = {
	PROPERTY_ENTRY_STRING("name", "wifi_rfkill"),
	PROPERTY_ENTRY_STRING("type", "wlan"),
	{ },
};

static struct platform_device wifi_rfkill_device = {
	.name	= "rfkill_gpio",
	.id	= -1,
};

static struct gpiod_lookup_table wifi_gpio_lookup = {
	.dev_id = "rfkill_gpio",
	.table = {
		GPIO_LOOKUP("tegra-gpio", 25, "reset", 0),
		GPIO_LOOKUP("tegra-gpio", 85, "shutdown", 0),
		{ },
	},
};

void __init tegra_paz00_wifikill_init(void)
{
	platform_device_add_properties(&wifi_rfkill_device, wifi_rfkill_prop);
	gpiod_add_lookup_table(&wifi_gpio_lookup);
	platform_device_register(&wifi_rfkill_device);
}
