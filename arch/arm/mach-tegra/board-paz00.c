// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/mach-tegra/board-paz00.c
 *
 * Copyright (C) 2011 Marc Dietrich <marvin24@gmx.de>
 *
 * Based on board-harmony.c
 * Copyright (C) 2010 Google, Inc.
 */

#include <linux/err.h>
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/property.h>

#include "board.h"

static const struct software_node tegra_gpiochip_node = {
	.name	= "tegra-gpio",
};

static const struct property_entry wifi_rfkill_prop[] __initconst = {
	PROPERTY_ENTRY_STRING("name", "wifi_rfkill"),
	PROPERTY_ENTRY_STRING("type", "wlan"),
	PROPERTY_ENTRY_GPIO("reset-gpios",
			    &tegra_gpiochip_node, 25, GPIO_ACTIVE_HIGH),
	PROPERTY_ENTRY_GPIO("shutdown-gpios",
			    &tegra_gpiochip_node, 85, GPIO_ACTIVE_HIGH),
	{ }
};

static const struct platform_device_info wifi_rfkill_info __initconst = {
	.name		= "rfkill_gpio",
	.id		= PLATFORM_DEVID_NONE,
	.properties	= wifi_rfkill_prop,
};

void __init tegra_paz00_wifikill_init(void)
{
	struct platform_device *pd;
	int err;

	err = software_node_register(&tegra_gpiochip_node);
	if (err) {
		pr_err("failed to register %s node: %d\n",
		       tegra_gpiochip_node.name, err);
		return;
	}

	pd = platform_device_register_full(&wifi_rfkill_info);
	err = PTR_ERR_OR_ZERO(pd);
	if (err)
		pr_err("failed to register WiFi rfkill device: %d\n", err);
}
