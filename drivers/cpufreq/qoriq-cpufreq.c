// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * CPU Frequency Scaling driver for Freescale QorIQ SoCs.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/cpufreq.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/platform_device.h>

/**
 * struct cpu_data
 * @pclk: the parent clock of cpu
 * @table: frequency table
 */
struct cpu_data {
	struct clk **pclk;
	struct cpufreq_frequency_table *table;
};

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
	int i, count;
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
	if (!data->pclk)
		goto err_nomem2;

	table = kcalloc(count + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		goto err_pclk;

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
	policy->freq_table = table;
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

static struct cpufreq_driver qoriq_cpufreq_driver = {
	.name		= "qoriq_cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS |
			  CPUFREQ_IS_COOLING_DEV,
	.init		= qoriq_cpufreq_cpu_init,
	.exit		= qoriq_cpufreq_cpu_exit,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= qoriq_cpufreq_target,
	.get		= cpufreq_generic_get,
	.attr		= cpufreq_generic_attr,
};

static const struct of_device_id qoriq_cpufreq_blacklist[] = {
	/* e6500 cannot use cpufreq due to erratum A-008083 */
	{ .compatible = "fsl,b4420-clockgen", },
	{ .compatible = "fsl,b4860-clockgen", },
	{ .compatible = "fsl,t2080-clockgen", },
	{ .compatible = "fsl,t4240-clockgen", },
	{}
};

static int qoriq_cpufreq_probe(struct platform_device *pdev)
{
	int ret;
	struct device_node *np;

	np = of_find_matching_node(NULL, qoriq_cpufreq_blacklist);
	if (np) {
		of_node_put(np);
		dev_info(&pdev->dev, "Disabling due to erratum A-008083");
		return -ENODEV;
	}

	ret = cpufreq_register_driver(&qoriq_cpufreq_driver);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "Freescale QorIQ CPU frequency scaling driver\n");
	return 0;
}

static int qoriq_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&qoriq_cpufreq_driver);

	return 0;
}

static struct platform_driver qoriq_cpufreq_platform_driver = {
	.driver = {
		.name = "qoriq-cpufreq",
	},
	.probe = qoriq_cpufreq_probe,
	.remove = qoriq_cpufreq_remove,
};
module_platform_driver(qoriq_cpufreq_platform_driver);

MODULE_ALIAS("platform:qoriq-cpufreq");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tang Yuantian <Yuantian.Tang@freescale.com>");
MODULE_DESCRIPTION("cpufreq driver for Freescale QorIQ series SoCs");
