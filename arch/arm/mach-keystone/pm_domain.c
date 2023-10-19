// SPDX-License-Identifier: GPL-2.0-only
/*
 * PM domain driver for Keystone2 devices
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 *
 * Based on Kevins work on DAVINCI SOCs
 *	Kevin Hilman <khilman@linaro.org>
 */

#include <linux/init.h>
#include <linux/pm_runtime.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "keystone.h"

static struct dev_pm_domain keystone_pm_domain = {
	.ops = {
		USE_PM_CLK_RUNTIME_OPS
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static struct pm_clk_notifier_block platform_domain_notifier = {
	.pm_domain = &keystone_pm_domain,
	.con_ids = { NULL },
};

static const struct of_device_id of_keystone_table[] = {
	{.compatible = "ti,k2hk"},
	{.compatible = "ti,k2e"},
	{.compatible = "ti,k2l"},
	{ /* end of list */ },
};

int __init keystone_pm_runtime_init(void)
{
	struct device_node *np;

	np = of_find_matching_node(NULL, of_keystone_table);
	if (!np)
		return 0;

	pm_clk_add_notifier(&platform_bus_type, &platform_domain_notifier);

	return 0;
}
