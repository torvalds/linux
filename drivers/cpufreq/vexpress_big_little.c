/*
 * Vexpress big.LITTLE CPUFreq Interface driver
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
#include <linux/export.h>
#include <linux/opp.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/vexpress.h>
#include "arm_big_little.h"

static int vexpress_init_opp_table(struct device *cpu_dev)
{
	int i = -1, count, cluster = cpu_to_cluster(cpu_dev->id);
	u32 *table;
	int ret;

	count = vexpress_spc_get_freq_table(cluster, &table);
	if (!table || !count) {
		pr_err("SPC controller returned invalid freq table");
		return -EINVAL;
	}

	while (++i < count) {
		/* FIXME: Voltage value */
		ret = opp_add(cpu_dev, table[i] * 1000, 900000);
		if (ret) {
			dev_warn(cpu_dev, "%s: Failed to add OPP %d, err: %d\n",
				 __func__, table[i] * 1000, ret);
			return ret;
		}
	}

	return 0;
}

static int vexpress_get_transition_latency(struct device *cpu_dev)
{
	/* 1 ms */
	return 1000000;
}

static struct cpufreq_arm_bL_ops vexpress_bL_ops = {
	.name	= "vexpress-bL",
	.get_transition_latency = vexpress_get_transition_latency,
	.init_opp_table = vexpress_init_opp_table,
};

static int vexpress_bL_init(void)
{
	if (!vexpress_spc_check_loaded()) {
		pr_info("%s: No SPC found\n", __func__);
		return -ENOENT;
	}

	return bL_cpufreq_register(&vexpress_bL_ops);
}
module_init(vexpress_bL_init);

static void vexpress_bL_exit(void)
{
	return bL_cpufreq_unregister(&vexpress_bL_ops);
}
module_exit(vexpress_bL_exit);

MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("ARM Vexpress big LITTLE cpufreq driver");
MODULE_LICENSE("GPL");
