// SPDX-License-Identifier: GPL-2.0
/*
 * Siemens SIMATIC IPC driver for CMOS battery monitoring
 *
 * Copyright (c) Siemens AG, 2023
 *
 * Authors:
 *  Henning Schild <henning.schild@siemens.com>
 */

#include <linux/gpio/machine.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/x86/simatic-ipc-base.h>

#include "simatic-ipc-batt.h"

static struct gpiod_lookup_table simatic_ipc_batt_gpio_table_227g = {
	.table = {
		GPIO_LOOKUP_IDX("gpio-f7188x-7", 6, NULL, 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio-f7188x-7", 5, NULL, 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:01",  66, NULL, 2, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static struct gpiod_lookup_table simatic_ipc_batt_gpio_table_bx_39a = {
	.table = {
		GPIO_LOOKUP_IDX("gpio-f7188x-6", 4, NULL, 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("gpio-f7188x-6", 3, NULL, 1, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static int simatic_ipc_batt_f7188x_remove(struct platform_device *pdev)
{
	const struct simatic_ipc_platform *plat = pdev->dev.platform_data;

	if (plat->devmode == SIMATIC_IPC_DEVICE_227G)
		return simatic_ipc_batt_remove(pdev, &simatic_ipc_batt_gpio_table_227g);

	return simatic_ipc_batt_remove(pdev, &simatic_ipc_batt_gpio_table_bx_39a);
}

static int simatic_ipc_batt_f7188x_probe(struct platform_device *pdev)
{
	const struct simatic_ipc_platform *plat = pdev->dev.platform_data;

	if (plat->devmode == SIMATIC_IPC_DEVICE_227G)
		return simatic_ipc_batt_probe(pdev, &simatic_ipc_batt_gpio_table_227g);

	return simatic_ipc_batt_probe(pdev, &simatic_ipc_batt_gpio_table_bx_39a);
}

static struct platform_driver simatic_ipc_batt_driver = {
	.probe = simatic_ipc_batt_f7188x_probe,
	.remove = simatic_ipc_batt_f7188x_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(simatic_ipc_batt_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-batt gpio_f7188x platform:elkhartlake-pinctrl");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
