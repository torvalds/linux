/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * CPU Frequency Scaling driver for Freescale QorIQ SoCs.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/smp.h>

/**
 * struct cpu_data
 * @pclk: the parent clock of cpu
 * @table: frequency table
 */
struct cpu_data {
	struct clk **pclk;
	struct cpufreq_frequency_table *table;
	struct thermal_cooling_device *cdev;
};

/*
 * Don't use cpufreq on this SoC -- used when the SoC would have otherwise
 * matched a more generic compatible.
 */
#define SOC_BLACKLIST		1

/**
 * struct soc_data - SoC specific data
 * @flags: SOC_xxx
 */
struct soc_data {
	u32 flags;
};

static u32 get_bus_freq(void)
{
	struct device_node *soc;
	u32 sysfreq;
	struct clk *pltclk;
	int ret;

	/* get platform freq by searching bus-frequency property */
	soc = of_find_node_by_type(NULL, "soc");
	if (soc) {
		ret = of_property_read_u32(soc, "bus-frequency", &sysfreq);
		of_node_put(soc);
		if (!ret)
			return sysfreq;
	}

	/* get platform freq by its clock name */
	pltclk = clk_get(NULL, "cg-pll0-div1");
	if (IS_ERR(pltclk)) {
		pr_err("%s: can't get bus frequency %ld\n",
		       __func__, PTR_ERR(pltclk));
		return PTR_ERR(pltclk);
	}

	return clk_get_rate(pltclk);
}

static struct clk *cpu_to_clk(int cpu)
{
	struct device_node *np;
	struct clk *clk;

	if (!cpu_present(cpu))
		return NULL;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		return NULL;

	clk = of_clk_get(np, 0);
	of_node_put(np);
	return clk;
}

/* traverse cpu nodes to get cpu mask of sharing clock wire */
static void set_affected_cpus(struct cpufreq_policy *policy)
{
	struct cpumask *dstp = policy->cpus;
	struct clk *clk;
	int i;

	for_each_present_cpu(i) {
		clk = cpu_to_clk(i);
		if (IS_ERR(clk)) {
			pr_err("%s: no clock for cpu %d\n", __func__, i);
			continue;
		}

		if (clk_is_match(policy->clk, clk))
			cpumask_set_cpu(i, dstp);
	}
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

static int qoriq_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct device_node *np;
	int i, count, ret;
	u32 freq;
	struct clk *clk;
	const struct clk_hw *hwclk;
	struct cpufreq_frequency_table *table;
	struct cpu_data *data;
	unsigned int cpu = policy->cpu;
	u64 u64temp;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		goto err_np;

	policy->clk = of_clk_get(np, 0);
	if (IS_ERR(policy->clk)) {
		pr_err("%s: no clock information\n", __func__);
		goto err_nomem2;
	}

	hwclk = __clk_get_hw(policy->clk);
	count = clk_hw_get_num_parents(hwclk);

	data->pclk = kcalloc(count, sizeof(struct clk *), GFP_KERNEL);
	if (!data->pclk) {
		pr_err("%s: no memory\n", __func__);
		goto err_nomem2;
	}

	table = kcalloc(count + 1, sizeof(*table), GFP_KERNEL);
	if (!table) {
		pr_err("%s: no memory\n", __func__);
		goto err_pclk;
	}

	for (i = 0; i < count; i++) {
		clk = clk_hw_get_parent_by_index(hwclk, i)->clk;
		data->pclk[i] = clk;
		freq = clk_get_rate(clk);
		table[i].frequency = freq / 1000;
		table[i].driver_data = i;
	}
	freq_table_redup(table, count);
	freq_table_sort(table, count);
	table[i].frequency = CPUFREQ_TABLE_END;

	/* set the min and max frequency properly */
	ret = cpufreq_table_validate_and_show(policy, table);
	if (ret) {
		pr_err("invalid frequency table: %d\n", ret);
		goto err_nomem1;
	}

	data->table = table;

	/* update ->cpus if we have cluster, no harm if not */
	set_affected_cpus(policy);
	policy->driver_data = data;

	/* Minimum transition latency is 12 platform clocks */
	u64temp = 12ULL * NSEC_PER_SEC;
	do_div(u64temp, get_bus_freq());
	policy->cpuinfo.transition_latency = u64temp + 1;

	of_node_put(np);

	return 0;

err_nomem1:
	kfree(table);
err_pclk:
	kfree(data->pclk);
err_nomem2:
	kfree(data);
err_np:
	of_node_put(np);

	return -ENODEV;
}

static int qoriq_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	struct cpu_data *data = policy->driver_data;

	cpufreq_cooling_unregister(data->cdev);
	kfree(data->pclk);
	kfree(data->table);
	kfree(data);
	policy->driver_data = NULL;

	return 0;
}

static int qoriq_cpufreq_target(struct cpufreq_policy *policy,
		unsigned int index)
{
	struct clk *parent;
	struct cpu_data *data = policy->driver_data;

	parent = data->pclk[data->table[index].driver_data];
	return clk_set_parent(policy->clk, parent);
}


static void qoriq_cpufreq_ready(struct cpufreq_policy *policy)
{
	struct cpu_data *cpud = policy->driver_data;
	struct device_node *np = of_get_cpu_node(policy->cpu, NULL);

	if (of_find_property(np, "#cooling-cells", NULL)) {
		cpud->cdev = of_cpufreq_cooling_register(np,
							 policy->related_cpus);

		if (IS_ERR(cpud->cdev) && PTR_ERR(cpud->cdev) != -ENOSYS) {
			pr_err("cpu%d is not running as cooling device: %ld\n",
					policy->cpu, PTR_ERR(cpud->cdev));

			cpud->cdev = NULL;
		}
	}

	of_node_put(np);
}

static struct cpufreq_driver qoriq_cpufreq_driver = {
	.name		= "qoriq_cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= qoriq_cpufreq_cpu_init,
	.exit		= qoriq_cpufreq_cpu_exit,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= qoriq_cpufreq_target,
	.get		= cpufreq_generic_get,
	.ready		= qoriq_cpufreq_ready,
	.attr		= cpufreq_generic_attr,
};

static const struct soc_data blacklist = {
	.flags = SOC_BLACKLIST,
};

static const struct of_device_id node_matches[] __initconst = {
	/* e6500 cannot use cpufreq due to erratum A-008083 */
	{ .compatible = "fsl,b4420-clockgen", &blacklist },
	{ .compatible = "fsl,b4860-clockgen", &blacklist },
	{ .compatible = "fsl,t2080-clockgen", &blacklist },
	{ .compatible = "fsl,t4240-clockgen", &blacklist },

	{ .compatible = "fsl,ls1012a-clockgen", },
	{ .compatible = "fsl,ls1021a-clockgen", },
	{ .compatible = "fsl,ls1043a-clockgen", },
	{ .compatible = "fsl,ls1046a-clockgen", },
	{ .compatible = "fsl,ls1088a-clockgen", },
	{ .compatible = "fsl,ls2080a-clockgen", },
	{ .compatible = "fsl,p4080-clockgen", },
	{ .compatible = "fsl,qoriq-clockgen-1.0", },
	{ .compatible = "fsl,qoriq-clockgen-2.0", },
	{}
};

static int __init qoriq_cpufreq_init(void)
{
	int ret;
	struct device_node  *np;
	const struct of_device_id *match;
	const struct soc_data *data;

	np = of_find_matching_node(NULL, node_matches);
	if (!np)
		return -ENODEV;

	match = of_match_node(node_matches, np);
	data = match->data;

	of_node_put(np);

	if (data && data->flags & SOC_BLACKLIST)
		return -ENODEV;

	ret = cpufreq_register_driver(&qoriq_cpufreq_driver);
	if (!ret)
		pr_info("Freescale QorIQ CPU frequency scaling driver\n");

	return ret;
}
module_init(qoriq_cpufreq_init);

static void __exit qoriq_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&qoriq_cpufreq_driver);
}
module_exit(qoriq_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tang Yuantian <Yuantian.Tang@freescale.com>");
MODULE_DESCRIPTION("cpufreq driver for Freescale QorIQ series SoCs");
