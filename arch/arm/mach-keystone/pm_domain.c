/*
 * PM domain driver for Keystone2 devices
 *
 * Copyright 2013 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shillimkar@ti.com>
 *
 * Based on Kevins work on DAVINCI SOCs
 *	Kevin Hilman <khilman@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pm_runtime.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

#ifdef CONFIG_PM_RUNTIME
static int keystone_pm_runtime_suspend(struct device *dev)
{
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	ret = pm_generic_runtime_suspend(dev);
	if (ret)
		return ret;

	ret = pm_clk_suspend(dev);
	if (ret) {
		pm_generic_runtime_resume(dev);
		return ret;
	}

	return 0;
}

static int keystone_pm_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);

	pm_clk_resume(dev);

	return pm_generic_runtime_resume(dev);
}
#endif

static struct dev_pm_domain keystone_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(keystone_pm_runtime_suspend,
				   keystone_pm_runtime_resume, NULL)
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static struct pm_clk_notifier_block platform_domain_notifier = {
	.pm_domain = &keystone_pm_domain,
};

static struct of_device_id of_keystone_table[] = {
	{.compatible = "ti,keystone"},
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
