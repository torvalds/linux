/*
 * Runtime PM support code for OMAP
 *
 * Author: Kevin Hilman, Deep Root Systems, LLC
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>

#include <plat/omap_device.h>
#include <plat/omap-pm.h>

#ifdef CONFIG_PM_RUNTIME
static int omap_pm_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int r, ret = 0;

	dev_dbg(dev, "%s\n", __func__);

	ret = pm_generic_runtime_suspend(dev);

	if (!ret && dev->parent == &omap_device_parent) {
		r = omap_device_idle(pdev);
		WARN_ON(r);
	}

	return ret;
};

static int omap_pm_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int r;

	dev_dbg(dev, "%s\n", __func__);

	if (dev->parent == &omap_device_parent) {
		r = omap_device_enable(pdev);
		WARN_ON(r);
	}

	return pm_generic_runtime_resume(dev);
};
#else
#define omap_pm_runtime_suspend NULL
#define omap_pm_runtime_resume NULL
#endif /* CONFIG_PM_RUNTIME */

static int __init omap_pm_runtime_init(void)
{
	const struct dev_pm_ops *pm;
	struct dev_pm_ops *omap_pm;

	pm = platform_bus_get_pm_ops();
	if (!pm) {
		pr_err("%s: unable to get dev_pm_ops from platform_bus\n",
			__func__);
		return -ENODEV;
	}

	omap_pm = kmemdup(pm, sizeof(struct dev_pm_ops), GFP_KERNEL);
	if (!omap_pm) {
		pr_err("%s: unable to alloc memory for new dev_pm_ops\n",
			__func__);
		return -ENOMEM;
	}

	omap_pm->runtime_suspend = omap_pm_runtime_suspend;
	omap_pm->runtime_resume = omap_pm_runtime_resume;

	platform_bus_set_pm_ops(omap_pm);

	return 0;
}
core_initcall(omap_pm_runtime_init);
