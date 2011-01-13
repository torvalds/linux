/*
 * Runtime PM support code for OMAP1
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
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/omap_device.h>
#include <plat/omap-pm.h>

#ifdef CONFIG_PM_RUNTIME
static int omap1_pm_runtime_suspend(struct device *dev)
{
	struct clk *iclk, *fclk;
	int ret = 0;

	dev_dbg(dev, "%s\n", __func__);

	ret = pm_generic_runtime_suspend(dev);

	fclk = clk_get(dev, "fck");
	if (!IS_ERR(fclk)) {
		clk_disable(fclk);
		clk_put(fclk);
	}

	iclk = clk_get(dev, "ick");
	if (!IS_ERR(iclk)) {
		clk_disable(iclk);
		clk_put(iclk);
	}

	return 0;
};

static int omap1_pm_runtime_resume(struct device *dev)
{
	struct clk *iclk, *fclk;

	dev_dbg(dev, "%s\n", __func__);

	iclk = clk_get(dev, "ick");
	if (!IS_ERR(iclk)) {
		clk_enable(iclk);
		clk_put(iclk);
	}

	fclk = clk_get(dev, "fck");
	if (!IS_ERR(fclk)) {
		clk_enable(fclk);
		clk_put(fclk);
	}

	return pm_generic_runtime_resume(dev);
};

static int __init omap1_pm_runtime_init(void)
{
	const struct dev_pm_ops *pm;
	struct dev_pm_ops *omap_pm;

	if (!cpu_class_is_omap1())
		return -ENODEV;

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

	omap_pm->runtime_suspend = omap1_pm_runtime_suspend;
	omap_pm->runtime_resume = omap1_pm_runtime_resume;

	platform_bus_set_pm_ops(omap_pm);

	return 0;
}
core_initcall(omap1_pm_runtime_init);
#endif /* CONFIG_PM_RUNTIME */
