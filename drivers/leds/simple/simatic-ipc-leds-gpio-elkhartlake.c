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
		GPIO_LOOKUP_IDX("INTC1020:04", 72, NULL, 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 77, NULL, 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 78, NULL, 2, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 58, NULL, 3, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 60, NULL, 4, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 62, NULL, 5, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static int simatic_ipc_leds_gpio_elkhartlake_probe(struct platform_device *pdev)
{
	return simatic_ipc_leds_gpio_probe(pdev, &simatic_ipc_led_gpio_table,
					   NULL);
}

static void simatic_ipc_leds_gpio_elkhartlake_remove(struct platform_device *pdev)
{
	simatic_ipc_leds_gpio_remove(pdev, &simatic_ipc_led_gpio_table, NULL);
}

static struct platform_driver simatic_ipc_led_gpio_elkhartlake_driver = {
	.probe = simatic_ipc_leds_gpio_elkhartlake_probe,
	.remove_new = simatic_ipc_leds_gpio_elkhartlake_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};
module_platform_driver(simatic_ipc_led_gpio_elkhartlake_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-leds-gpio-core platform:elkhartlake-pinctrl");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
