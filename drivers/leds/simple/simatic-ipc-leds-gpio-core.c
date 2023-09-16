// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for GPIO based LEDs
 *
 * Copyright (c) Siemens AG, 2023
 *
 * Author:
 *  Henning Schild <henning.schild@siemens.com>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>

#include "simatic-ipc-leds-gpio.h"

static struct platform_device *simatic_leds_pdev;

static const struct gpio_led simatic_ipc_gpio_leds[] = {
	{ .name = "red:" LED_FUNCTION_STATUS "-1" },
	{ .name = "green:" LED_FUNCTION_STATUS "-1" },
	{ .name = "red:" LED_FUNCTION_STATUS "-2" },
	{ .name = "green:" LED_FUNCTION_STATUS "-2" },
	{ .name = "red:" LED_FUNCTION_STATUS "-3" },
	{ .name = "green:" LED_FUNCTION_STATUS "-3" },
};

static const struct gpio_led_platform_data simatic_ipc_gpio_leds_pdata = {
	.num_leds	= ARRAY_SIZE(simatic_ipc_gpio_leds),
	.leds		= simatic_ipc_gpio_leds,
};

void simatic_ipc_leds_gpio_remove(struct platform_device *pdev,
				 struct gpiod_lookup_table *table,
				 struct gpiod_lookup_table *table_extra)
{
	gpiod_remove_lookup_table(table);
	gpiod_remove_lookup_table(table_extra);
	platform_device_unregister(simatic_leds_pdev);
}
EXPORT_SYMBOL_GPL(simatic_ipc_leds_gpio_remove);

int simatic_ipc_leds_gpio_probe(struct platform_device *pdev,
				struct gpiod_lookup_table *table,
				struct gpiod_lookup_table *table_extra)
{
	const struct simatic_ipc_platform *plat = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct gpio_desc *gpiod;
	int err;

	switch (plat->devmode) {
	case SIMATIC_IPC_DEVICE_127E:
	case SIMATIC_IPC_DEVICE_227G:
	case SIMATIC_IPC_DEVICE_BX_21A:
		break;
	default:
		return -ENODEV;
	}

	gpiod_add_lookup_table(table);
	simatic_leds_pdev = platform_device_register_resndata(NULL,
		"leds-gpio", PLATFORM_DEVID_NONE, NULL, 0,
		&simatic_ipc_gpio_leds_pdata,
		sizeof(simatic_ipc_gpio_leds_pdata));
	if (IS_ERR(simatic_leds_pdev)) {
		err = PTR_ERR(simatic_leds_pdev);
		goto out;
	}

	if (!table_extra)
		return 0;

	table_extra->dev_id = dev_name(dev);
	gpiod_add_lookup_table(table_extra);

	/* PM_BIOS_BOOT_N */
	gpiod = gpiod_get_index(dev, NULL, 6, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		err = PTR_ERR(gpiod);
		goto out;
	}
	gpiod_put(gpiod);

	/* PM_WDT_OUT */
	gpiod = gpiod_get_index(dev, NULL, 7, GPIOD_OUT_LOW);
	if (IS_ERR(gpiod)) {
		err = PTR_ERR(gpiod);
		goto out;
	}
	gpiod_put(gpiod);

	return 0;
out:
	simatic_ipc_leds_gpio_remove(pdev, table, table_extra);

	return err;
}
EXPORT_SYMBOL_GPL(simatic_ipc_leds_gpio_probe);

MODULE_LICENSE("GPL v2");
MODULE_SOFTDEP("pre: platform:leds-gpio");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
