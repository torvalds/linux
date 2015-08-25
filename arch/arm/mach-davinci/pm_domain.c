/*
 * Runtime PM support code for DaVinci
 *
 * Author: Kevin Hilman
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/init.h>
#include <linux/pm_runtime.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>

static struct dev_pm_domain davinci_pm_domain = {
	.ops = {
		USE_PM_CLK_RUNTIME_OPS
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static struct pm_clk_notifier_block platform_bus_notifier = {
	.pm_domain = &davinci_pm_domain,
	.con_ids = { "fck", "master", "slave", NULL },
};

static int __init davinci_pm_runtime_init(void)
{
	pm_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);

	return 0;
}
core_initcall(davinci_pm_runtime_init);
