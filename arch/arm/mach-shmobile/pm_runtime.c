/*
 * arch/arm/mach-shmobile/pm_runtime.c
 *
 * Runtime PM support code for SuperH Mobile ARM
 *
 *  Copyright (C) 2009-2010 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/bitmap.h>
#include <linux/slab.h>

#ifdef CONFIG_PM_RUNTIME

static int default_platform_runtime_idle(struct device *dev)
{
	/* suspend synchronously to disable clocks immediately */
	return pm_runtime_suspend(dev);
}

static struct dev_power_domain default_power_domain = {
	.ops = {
		.runtime_suspend = pm_runtime_clk_suspend,
		.runtime_resume = pm_runtime_clk_resume,
		.runtime_idle = default_platform_runtime_idle,
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

#define DEFAULT_PWR_DOMAIN_PTR	(&default_power_domain)

#else

#define DEFAULT_PWR_DOMAIN_PTR	NULL

#endif /* CONFIG_PM_RUNTIME */

static struct pm_clk_notifier_block platform_bus_notifier = {
	.pwr_domain = DEFAULT_PWR_DOMAIN_PTR,
	.con_ids = { NULL, },
};

static int __init sh_pm_runtime_init(void)
{
	pm_runtime_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);
	return 0;
}
core_initcall(sh_pm_runtime_init);
