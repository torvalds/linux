/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * CPU Frequency Scaling driver for Freescale PowerPC corenet SoCs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <sysdev/fsl_soc.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/smp.h>

/**
 * struct cpu_data - per CPU data struct
 * @clk: the clk of CPU
 * @parent: the parent node of cpu clock
 * @table: frequency table
 */
struct cpu_data {
	struct clk *clk;
	struct device_node *parent;
	struct cpufreq_frequency_table *table;
};

/**
 * struct soc_data - SoC specific data
 * @freq_mask: mask the disallowed frequencies
 * @flag: unique flags
 */
struct soc_data {
	u32 freq_mask[4];
	u32 flag;
};

#define FREQ_MASK	1
/* see hardware specification for the allowed frqeuencies */
static const struct soc_data sdata[] = {
	{ /* used by p2041 and p3041 */
		.freq_mask = {0x8, 0x8, 0x2, 0x2},
		.flag = FREQ_MASK,
	},
	{ /* used by p5020 */
		.freq_mask = {0x8, 0x2},
		.flag = FREQ_MASK,
	},
	{ /* used by p4080, p5040 */
		.freq_mask = {0},
		.flag = 0,
	},
};

/*
 * the minimum allowed core frequency, in Hz
 * for chassis v1.0, >= platform frequency
 * for chassis v2.0, >= platform frequency / 2
 */
static u32 min_cpufreq;
static const u32 *fmask;

/* serialize frequency changes  */
static DEFINE_MUTEX(cpufreq_lock);
static DEFINE_PER_CPU(struct cpu_data *, cpu_data);

/* cpumask in a cluster */
static DEFINE_PER_CPU(cpumask_var_t, cpu_mask);

#ifndef CONFIG_SMP
static inline const struct cpumask *cpu_core_mask(int cpu)
{
	return cpumask_of(0);
}
#endif

static unsigned int corenet_cpufreq_get_speed(unsigned int cpu)
{
	struct cpu_data *data = per_cpu(cpu_data, cpu);

	return clk_get_rate(data->clk) / 1000;
}

/* reduce the duplicated frequencies in frequency table */
static void freq_table_redup(struct cpufreq_frequency_table *freq_table,
		int count)
{
	int i, j;

	for (i = 1; i < count; i++) {
		for (j = 0; j < i; j++) {
			if (freq_table[j].frequency == CPUFREQ_ENTRY_INVALID ||
					freq_table[j].frequency !=
					freq_table[i].frequency)
				continue;

			freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;
			break;
		}
	}
}

/* sort the frequencies in frequency table in descenting order */
static void freq_table_sort(struct cpufreq_frequency_table *freq_table,
		int count)
{
	int i, j, ind;
	unsigned int freq, max_freq;
	struct cpufreq_frequency_table table;
	for (i = 0; i < count - 1; i++) {
		max_freq = freq_table[i].frequency;
		ind = i;
		for (j = i + 1; j < count; j++) {
			freq = freq_table[j].frequency;
			if (freq == CPUFREQ_ENTRY_INVALID ||
					freq <= max_freq)
				continue;
			ind = j;
			max_freq = freq;
		}

		if (ind != i) {
			/* exchange the frequencies */
			table.driver_data = freq_table[i].driver_data;
			table.frequency = freq_table[i].frequency;
			freq_table[i].driver_data = freq_table[ind].driver_data;
			freq_table[i].frequency = freq_table[ind].frequency;
			freq_table[ind].driver_data = table.driver_data;
			freq_table[ind].frequency = table.frequency;
		}
	}
}

static int corenet_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct device_node *np;
	int i, count, ret;
	u32 freq, mask;
	struct clk *clk;
	struct cpufreq_frequency_table *table;
	struct cpu_data *data;
	unsigned int cpu = policy->cpu;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("%s: no memory\n", __func__);
		goto err_np;
	}

	data->clk = of_clk_get(np, 0);
	if (IS_ERR(data->clk)) {
		pr_err("%s: no clock information\n", __func__);
		goto err_nomem2;
	}

	data->parent = of_parse_phandle(np, "clocks", 0);
	if (!data->parent) {
		pr_err("%s: could not get clock information\n", __func__);
		goto err_nomem2;
	}

	count = of_property_count_strings(data->parent, "clock-names");
	table = kcalloc(count + 1, sizeof(*table), GFP_KERNEL);
	if (!table) {
		pr_err("%s: no memory\n", __func__);
		goto err_node;
	}

	if (fmask)
		mask = fmask[get_hard_smp_processor_id(cpu)];
	else
		mask = 0x0;

	for (i = 0; i < count; i++) {
		clk = of_clk_get(data->parent, i);
		freq = clk_get_rate(clk);
		/*
		 * the clock is valid if its frequency is not masked
		 * and large than minimum allowed frequency.
		 */
		if (freq < min_cpufreq || (mask & (1 << i)))
			table[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			table[i].frequency = freq / 1000;
		table[i].driver_data = i;
	}
	freq_table_redup(table, count);
	freq_table_sort(table, count);
	table[i].frequency = CPUFREQ_TABLE_END;

	/* set the min and max frequency properly */
	ret = cpufreq_frequency_table_cpuinfo(policy, table);
	if (ret) {
		pr_err("invalid frequency table: %d\n", ret);
		goto err_nomem1;
	}

	data->table = table;
	per_cpu(cpu_data, cpu) = data;

	/* update ->cpus if we have cluster, no harm if not */
	cpumask_copy(policy->cpus, per_cpu(cpu_mask, cpu));
	for_each_cpu(i, per_cpu(cpu_mask, cpu))
		per_cpu(cpu_data, i) = data;

	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur = corenet_cpufreq_get_speed(policy->cpu);

	cpufreq_frequency_table_get_attr(table, cpu);
	of_node_put(np);

	return 0;

err_nomem1:
	kfree(table);
err_node:
	of_node_put(data->parent);
err_nomem2:
	per_cpu(cpu_data, cpu) = NULL;
	kfree(data);
err_np:
	of_node_put(np);

	return -ENODEV;
}

static int __exit corenet_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	struct cpu_data *data = per_cpu(cpu_data, policy->cpu);
	unsigned int cpu;

	cpufreq_frequency_table_put_attr(policy->cpu);
	of_node_put(data->parent);
	kfree(data->table);
	kfree(data);

	for_each_cpu(cpu, per_cpu(cpu_mask, policy->cpu))
		per_cpu(cpu_data, cpu) = NULL;

	return 0;
}

static int corenet_cpufreq_verify(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *table =
		per_cpu(cpu_data, policy->cpu)->table;

	return cpufreq_frequency_table_verify(policy, table);
}

static int corenet_cpufreq_target(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	unsigned int new;
	struct clk *parent;
	int ret;
	struct cpu_data *data = per_cpu(cpu_data, policy->cpu);

	cpufreq_frequency_table_target(policy, data->table,
			target_freq, relation, &new);

	if (policy->cur == data->table[new].frequency)
		return 0;

	freqs.old = policy->cur;
	freqs.new = data->table[new].frequency;

	mutex_lock(&cpufreq_lock);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	parent = of_clk_get(data->parent, data->table[new].driver_data);
	ret = clk_set_parent(data->clk, parent);
	if (ret)
		freqs.new = freqs.old;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
	mutex_unlock(&cpufreq_lock);

	return ret;
}

static struct freq_attr *corenet_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver ppc_corenet_cpufreq_driver = {
	.name		= "ppc_cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= corenet_cpufreq_cpu_init,
	.exit		= __exit_p(corenet_cpufreq_cpu_exit),
	.verify		= corenet_cpufreq_verify,
	.target		= corenet_cpufreq_target,
	.get		= corenet_cpufreq_get_speed,
	.attr		= corenet_cpufreq_attr,
};

static const struct of_device_id node_matches[] __initdata = {
	{ .compatible = "fsl,p2041-clockgen", .data = &sdata[0], },
	{ .compatible = "fsl,p3041-clockgen", .data = &sdata[0], },
	{ .compatible = "fsl,p5020-clockgen", .data = &sdata[1], },
	{ .compatible = "fsl,p4080-clockgen", .data = &sdata[2], },
	{ .compatible = "fsl,p5040-clockgen", .data = &sdata[2], },
	{ .compatible = "fsl,qoriq-clockgen-2.0", },
	{}
};

static int __init ppc_corenet_cpufreq_init(void)
{
	int ret;
	struct device_node  *np;
	const struct of_device_id *match;
	const struct soc_data *data;
	unsigned int cpu;

	np = of_find_matching_node(NULL, node_matches);
	if (!np)
		return -ENODEV;

	for_each_possible_cpu(cpu) {
		if (!alloc_cpumask_var(&per_cpu(cpu_mask, cpu), GFP_KERNEL))
			goto err_mask;
		cpumask_copy(per_cpu(cpu_mask, cpu), cpu_core_mask(cpu));
	}

	match = of_match_node(node_matches, np);
	data = match->data;
	if (data) {
		if (data->flag)
			fmask = data->freq_mask;
		min_cpufreq = fsl_get_sys_freq();
	} else {
		min_cpufreq = fsl_get_sys_freq() / 2;
	}

	of_node_put(np);

	ret = cpufreq_register_driver(&ppc_corenet_cpufreq_driver);
	if (!ret)
		pr_info("Freescale PowerPC corenet CPU frequency scaling driver\n");

	return ret;

err_mask:
	for_each_possible_cpu(cpu)
		free_cpumask_var(per_cpu(cpu_mask, cpu));

	return -ENOMEM;
}
module_init(ppc_corenet_cpufreq_init);

static void __exit ppc_corenet_cpufreq_exit(void)
{
	unsigned int cpu;

	for_each_possible_cpu(cpu)
		free_cpumask_var(per_cpu(cpu_mask, cpu));

	cpufreq_unregister_driver(&ppc_corenet_cpufreq_driver);
}
module_exit(ppc_corenet_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tang Yuantian <Yuantian.Tang@freescale.com>");
MODULE_DESCRIPTION("cpufreq driver for Freescale e500mc series SoCs");
