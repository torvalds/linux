/*
 * Runtime PM support code
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
#include <linux/pm_domain.h>
#include <linux/pm_clock.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/bitmap.h>
#include <linux/slab.h>

#ifdef CONFIG_PM_RUNTIME
static struct dev_pm_domain default_pm_domain = {
	.ops = {
		.runtime_suspend = pm_clk_suspend,
		.runtime_resume = pm_clk_resume,
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

#define DEFAULT_PM_DOMAIN_PTR	(&default_pm_domain)

#else

#define DEFAULT_PM_DOMAIN_PTR	NULL

#endif /* CONFIG_PM_RUNTIME */

static struct pm_clk_notifier_block platform_bus_notifier = {
	.pm_domain = DEFAULT_PM_DOMAIN_PTR,
	.con_ids = { NULL, },
};

static bool default_pm_on;

static int __init sh_pm_runtime_init(void)
{
	if (IS_ENABLED(CONFIG_ARCH_SHMOBILE_MULTI)) {
		if (!of_machine_is_compatible("renesas,emev2") &&
		    !of_machine_is_compatible("renesas,r7s72100") &&
		    !of_machine_is_compatible("renesas,r8a73a4") &&
		    !of_machine_is_compatible("renesas,r8a7740") &&
		    !of_machine_is_compatible("renesas,r8a7778") &&
		    !of_machine_is_compatible("renesas,r8a7779") &&
		    !of_machine_is_compatible("renesas,r8a7790") &&
		    !of_machine_is_compatible("renesas,r8a7791") &&
		    !of_machine_is_compatible("renesas,sh7372") &&
		    !of_machine_is_compatible("renesas,sh73a0"))
			return 0;
	}

	default_pm_on = true;
	pm_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);
	return 0;
}
core_initcall(sh_pm_runtime_init);

static int __init sh_pm_runtime_late_init(void)
{
	if (default_pm_on)
		pm_genpd_poweroff_unused();
	return 0;
}
late_initcall(sh_pm_runtime_late_init);
