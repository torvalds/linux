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

struct simatic_ipc_led_tables {
	struct gpiod_lookup_table *led_lookup_table;
	struct gpiod_lookup_table *led_lookup_table_extra;
};

static struct gpiod_lookup_table simatic_ipc_led_gpio_table_227g = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 0, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 1, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 2, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 3, NULL, 3, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 4, NULL, 4, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 5, NULL, 5, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	},
};

static struct gpiod_lookup_table simatic_ipc_led_gpio_table_extra_227g = {
	.dev_id = NULL, /* Filled during initialization */
	.table = {
		GPIO_LOOKUP_IDX("gpio-f7188x-3", 6, NULL, 6, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio-f7188x-3", 7, NULL, 7, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static struct gpiod_lookup_table simatic_ipc_led_gpio_table_bx_59a = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 0, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-2", 3, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-5", 3, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-5", 2, NULL, 3, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-7", 7, NULL, 4, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("gpio-f7188x-7", 4, NULL, 5, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	}
};

static int simatic_ipc_leds_gpio_f7188x_probe(struct platform_device *pdev)
{
	const struct simatic_ipc_platform *plat = dev_get_platdata(&pdev->dev);
	struct simatic_ipc_led_tables *led_tables;

	led_tables = devm_kzalloc(&pdev->dev, sizeof(*led_tables), GFP_KERNEL);
	if (!led_tables)
		return -ENOMEM;

	switch (plat->devmode) {
	case SIMATIC_IPC_DEVICE_227G:
		led_tables->led_lookup_table = &simatic_ipc_led_gpio_table_227g;
		led_tables->led_lookup_table_extra = &simatic_ipc_led_gpio_table_extra_227g;
		break;
	case SIMATIC_IPC_DEVICE_BX_59A:
		led_tables->led_lookup_table = &simatic_ipc_led_gpio_table_bx_59a;
		break;
	default:
		return -ENODEV;
	}

	platform_set_drvdata(pdev, led_tables);
	return simatic_ipc_leds_gpio_probe(pdev, led_tables->led_lookup_table,
					   led_tables->led_lookup_table_extra);
}

static void simatic_ipc_leds_gpio_f7188x_remove(struct platform_device *pdev)
{
	struct simatic_ipc_led_tables *led_tables = platform_get_drvdata(pdev);

	simatic_ipc_leds_gpio_remove(pdev, led_tables->led_lookup_table,
				     led_tables->led_lookup_table_extra);
}

static struct platform_driver simatic_ipc_led_gpio_driver = {
	.probe = simatic_ipc_leds_gpio_f7188x_probe,
	.remove_new = simatic_ipc_leds_gpio_f7188x_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};
module_platform_driver(simatic_ipc_led_gpio_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-leds-gpio-core gpio_f7188x");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
