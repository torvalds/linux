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

static struct gpiod_lookup_table simatic_ipc_batt_gpio_table_bx_21a = {
	.table = {
		GPIO_LOOKUP_IDX("INTC1020:04", 18, NULL, 0, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:04", 19, NULL, 1, GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP_IDX("INTC1020:01", 66, NULL, 2, GPIO_ACTIVE_HIGH),
		{} /* Terminating entry */
	},
};

static void simatic_ipc_batt_elkhartlake_remove(struct platform_device *pdev)
{
	simatic_ipc_batt_remove(pdev, &simatic_ipc_batt_gpio_table_bx_21a);
}

static int simatic_ipc_batt_elkhartlake_probe(struct platform_device *pdev)
{
	return simatic_ipc_batt_probe(pdev, &simatic_ipc_batt_gpio_table_bx_21a);
}

static struct platform_driver simatic_ipc_batt_driver = {
	.probe = simatic_ipc_batt_elkhartlake_probe,
	.remove_new = simatic_ipc_batt_elkhartlake_remove,
	.driver = {
		.name = KBUILD_MODNAME,
	},
};

module_platform_driver(simatic_ipc_batt_driver);

MODULE_DESCRIPTION("CMOS Battery monitoring for Simatic IPCs based on Elkhart Lake GPIO");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_SOFTDEP("pre: simatic-ipc-batt platform:elkhartlake-pinctrl");
MODULE_AUTHOR("Henning Schild <henning.schild@siemens.com>");
