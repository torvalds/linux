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
		if (!of_find_compatible_node(NULL, NULL,
					     "renesas,cpg-mstp-clocks"))
			return 0;
		if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS_OF) &&
		    of_find_node_with_property(NULL, "#power-domain-cells"))
			return 0;
	}

	pm_clk_add_notifier(&platform_bus_type, &platform_bus_notifier);
	return 0;
}
core_initcall(sh_pm_runtime_init);
