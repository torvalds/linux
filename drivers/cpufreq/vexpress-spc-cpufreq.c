/*
 * Versatile Express SPC CPUFreq Interface driver
 *
 * It provides necessary ops to arm_big_little cpufreq driver.
 *
 * Copyright (C) 2013 ARM Ltd.
 * Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/types.h>

#include "arm_big_little.h"

static int ve_spc_init_opp_table(struct device *cpu_dev)
{
	/*
	 * platform specific SPC code must initialise the opp table
	 * so just check if the OPP count is non-zero
	 */
	return dev_pm_opp_get_opp_count(cpu_dev) <= 0;
}

static int ve_spc_get_transition_latency(struct device *cpu_dev)
{
	return 1000000; /* 1 ms */
}

static struct cpufreq_arm_bL_ops ve_spc_cpufreq_ops = {
	.name	= "vexpress-spc",
	.get_transition_latency = ve_spc_get_transition_latency,
	.init_opp_table = ve_spc_init_opp_table,
};

static int ve_spc_cpufreq_probe(struct platform_device *pdev)
{
	return bL_cpufreq_register(&ve_spc_cpufreq_ops);
}

static int ve_spc_cpufreq_remove(struct platform_device *pdev)
{
	bL_cpufreq_unregister(&ve_spc_cpufreq_ops);
	return 0;
}

static struct platform_driver ve_spc_cpufreq_platdrv = {
	.driver = {
		.name	= "vexpress-spc-cpufreq",
	},
	.probe		= ve_spc_cpufreq_probe,
	.remove		= ve_spc_cpufreq_remove,
};
module_platform_driver(ve_spc_cpufreq_platdrv);

MODULE_LICENSE("GPL");
