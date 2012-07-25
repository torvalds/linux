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

#ifdef CONFIG_PM_RUNTIME
static int davinci_pm_runtime_suspend(struct device *dev)
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

static int davinci_pm_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);

	pm_clk_resume(dev);
	return pm_generic_runtime_resume(dev);
}
#endif

static struct dev_pm_domain davinci_pm_domain = {
	.ops = {
		SET_RUNTIME_PM_OPS(davinci_pm_runtime_suspend,
				   davinci_pm_runtime_resume, NULL)
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static struct pm_clk_notifier_block platform_bus_notifier = {
	.pm_domain = &davinci_pm_domain,
};

static int __init davinci_pm_runtime_init(void)
{
	pm_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);

	return 0;
}
core_initcall(davinci_pm_runtime_init);
