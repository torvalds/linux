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

static struct dev_pm_domain default_pm_domain = {
	.ops = {
		USE_PM_CLK_RUNTIME_OPS
		USE_PLATFORM_PM_SLEEP_OPS
	},
};

static struct pm_clk_notifier_block platform_bus_notifier = {
	.pm_domain = &default_pm_domain,
	.con_ids = { NULL, },
};

static int __init sh_pm_runtime_init(void)
{
	if (IS_ENABLED(CONFIG_ARCH_SHMOBILE_MULTI)) {
		if (!of_machine_is_compatible("renesas,emev2") &&
		    !of_machine_is_compatible("renesas,r7s72100") &&
#ifndef CONFIG_PM_GENERIC_DOMAINS_OF
		    !of_machine_is_compatible("renesas,r8a73a4") &&
		    !of_machine_is_compatible("renesas,r8a7740") &&
		    !of_machine_is_compatible("renesas,sh73a0") &&
#endif
		    !of_machine_is_compatible("renesas,r8a7778") &&
		    !of_machine_is_compatible("renesas,r8a7779") &&
		    !of_machine_is_compatible("renesas,r8a7790") &&
		    !of_machine_is_compatible("renesas,r8a7791") &&
		    !of_machine_is_compatible("renesas,r8a7792") &&
		    !of_machine_is_compatible("renesas,r8a7793") &&
		    !of_machine_is_compatible("renesas,r8a7794"))
			return 0;
	}

	pm_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);
	return 0;
}
core_initcall(sh_pm_runtime_init);
