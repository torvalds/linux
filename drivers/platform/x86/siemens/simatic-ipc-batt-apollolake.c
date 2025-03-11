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

#include "simatic-ipc-batt.h"

static struct gpiod_lookup_table simatic_ipc_batt_gpio_table_127e = {
	.table = {
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 55, NULL, 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.0", 61, NULL, 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("apollolake-pinctrl.1", 41, NULL, 2, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static void simatic_ipc_batt_apollolake_remove(struct platform_device *pdev)
{
	simatic_ipc_batt_remove(pdev, &simatic_ipc_batt_gpio_table_127e);
}

static int simatic_ipc_batt_apollolake_probe(struct platform_device *pdev)
{
	return simatic_ipc_batt_probe(pdev, &simatic_ipc_batt_gpio_table_127e);
}

static struct platform_driver simatic_ipc_batt_driver = {
	.probe = simatic_ipc_batt_apollolake_probe,
	.remove = simatic_ipc_batt_apollolake_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(simatic_ipc_batt_driver);

MODULE_DESCRIPTION("CMOS Battery monitoring for Simatic IPCs based on Apollo Lake GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-batt platform:apollolake-pinctrl");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
