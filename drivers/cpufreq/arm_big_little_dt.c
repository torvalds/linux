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
#include <linux/of.h>
#include <linux/opp.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "arm_big_little.h"

static int dt_init_opp_table(struct device *cpu_dev)
{
	struct device_node *np, *parent;
	int count = 0, ret;

	parent = of_find_node_by_path("/cpus");
	if (!parent) {
		pr_err("failed to find OF /cpus\n");
		return -ENOENT;
	}

	for_each_child_of_node(parent, np) {
		if (count++ != cpu_dev->id)
			continue;
		if (!of_get_property(np, "operating-points", NULL)) {
			ret = -ENODATA;
		} else {
			cpu_dev->of_node = np;
			ret = of_init_opp_table(cpu_dev);
		}
		of_node_put(np);
		of_node_put(parent);

		return ret;
	}

	return -ENODEV;
}

static int dt_get_transition_latency(struct device *cpu_dev)
{
	struct device_node *np, *parent;
	u32 transition_latency = CPUFREQ_ETERNAL;
	int count = 0;

	parent = of_find_node_by_path("/cpus");
	if (!parent) {
		pr_err("failed to find OF /cpus\n");
		return -ENOENT;
	}

	for_each_child_of_node(parent, np) {
		if (count++ != cpu_dev->id)
			continue;

		of_property_read_u32(np, "clock-latency", &transition_latency);
		of_node_put(np);
		of_node_put(parent);

		return transition_latency;
	}

	return -ENODEV;
}

static struct cpufreq_arm_bL_ops dt_bL_ops = {
	.name	= "dt-bl",
	.get_transition_latency = dt_get_transition_latency,
	.init_opp_table = dt_init_opp_table,
};

static int generic_bL_init(void)
{
	return bL_cpufreq_register(&dt_bL_ops);
}
module_init(generic_bL_init);

static void generic_bL_exit(void)
{
	return bL_cpufreq_unregister(&dt_bL_ops);
}
module_exit(generic_bL_exit);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("Generic ARM big LITTLE cpufreq driver via DT");
MODULE_LICENSE("GPL");
