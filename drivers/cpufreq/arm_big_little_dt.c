/*
 * Generic big.LITTLE CPUFreq Interface driver
 *
 * It provides necessary ops to arm_big_little cpufreq driver and gets
 * Frequency information from Device Tree. Freq table in DT must be in KHz.
 *
 * Copyright (C) 2013 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
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
#include <linux/device.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "arm_big_little.h"

/* get cpu node with valid operating-points */
static struct device_node *get_cpu_node_with_valid_op(int cpu)
{
	struct device_node *np = of_cpu_device_node_get(cpu);

	if (!of_get_property(np, "operating-points", NULL)) {
		of_node_put(np);
		np = NULL;
	}

	return np;
}

static int dt_init_opp_table(struct device *cpu_dev)
{
	struct device_node *np;
	int ret;

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		pr_err("failed to find cpu%d node\n", cpu_dev->id);
		return -ENOENT;
	}

	ret = of_init_opp_table(cpu_dev);
	of_node_put(np);

	return ret;
}

static int dt_get_transition_latency(struct device *cpu_dev)
{
	struct device_node *np;
	u32 transition_latency = CPUFREQ_ETERNAL;

	np = of_node_get(cpu_dev->of_node);
	if (!np) {
		pr_info("Failed to find cpu node. Use CPUFREQ_ETERNAL transition latency\n");
		return CPUFREQ_ETERNAL;
	}

	of_property_read_u32(np, "clock-latency", &transition_latency);
	of_node_put(np);

	pr_debug("%s: clock-latency: %d\n", __func__, transition_latency);
	return transition_latency;
}

static struct cpufreq_arm_bL_ops dt_bL_ops = {
	.name	= "dt-bl",
	.get_transition_latency = dt_get_transition_latency,
	.init_opp_table = dt_init_opp_table,
};

static int generic_bL_probe(struct platform_device *pdev)
{
	struct device_node *np;

	np = get_cpu_node_with_valid_op(0);
	if (!np)
		return -ENODEV;

	of_node_put(np);
	return bL_cpufreq_register(&dt_bL_ops);
}

static int generic_bL_remove(struct platform_device *pdev)
{
	bL_cpufreq_unregister(&dt_bL_ops);
	return 0;
}

static struct platform_driver generic_bL_platdrv = {
	.driver = {
		.name	= "arm-bL-cpufreq-dt",
	},
	.probe		= generic_bL_probe,
	.remove		= generic_bL_remove,
};
module_platform_driver(generic_bL_platdrv);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("Generic ARM big LITTLE cpufreq driver via DT");
MODULE_LICENSE("GPL v2");
