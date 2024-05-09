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

static struct gpiod_lookup_table simatic_ipc_led_gpio_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 52, NULL, 0, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 53, NULL, 1, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 57, NULL, 2, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 58, NULL, 3, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 60, NULL, 4, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 51, NULL, 5, GPIO_ACTIVE_LOW),
		{} /* Terminating entry */
	},
};

static struct gpiod_lookup_table simatic_ipc_led_gpio_table_extra = {
	.dev_id = NULL, /* Filled during initialization */
	.table = {
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 56, NULL, 6, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 59, NULL, 7, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static int simatic_ipc_leds_gpio_apollolake_probe(struct platform_device *pdev)
{
	return simatic_ipc_leds_gpio_probe(pdev, &simatic_ipc_led_gpio_table,
					   &simatic_ipc_led_gpio_table_extra);
}

static void simatic_ipc_leds_gpio_apollolake_remove(struct platform_device *pdev)
{
	simatic_ipc_leds_gpio_remove(pdev, &simatic_ipc_led_gpio_table,
				     &simatic_ipc_led_gpio_table_extra);
}

static struct platform_driver simatic_ipc_led_gpio_apollolake_driver = {
	.probe = simatic_ipc_leds_gpio_apollolake_probe,
	.remove_new = simatic_ipc_leds_gpio_apollolake_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};
module_platform_driver(simatic_ipc_led_gpio_apollolake_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-leds-gpio-core platform:apollolake-pinctrl");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
