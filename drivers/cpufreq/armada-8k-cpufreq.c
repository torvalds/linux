// SPDX-License-Identifier: GPL-2.0+
/*
 * CPUFreq support for Armada 8K
 *
 * Copyright (C) 2018 Marvell
 *
 * Omri Itach <omrii@marvell.com>
 * Gregory Clement <gregory.clement@bootlin.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

/*
 * Setup the opps list with the divider for the max frequency, that
 * will be filled at runtime.
 */
static const int opps_div[] __initconst = {1, 2, 3, 4};

static struct platform_device *armada_8k_pdev;

struct freq_table {
	struct device *cpu_dev;
	unsigned int freq[ARRAY_SIZE(opps_div)];
};

/* If the CPUs share the same clock, then they are in the same cluster. */
static void __init armada_8k_get_sharing_cpus(struct clk *cur_clk,
					      struct cpumask *cpumask)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct device *cpu_dev;
		struct clk *clk;

		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_warn("Failed to get cpu%d device\n", cpu);
			continue;
		}

		clk = clk_get(cpu_dev, 0);
		if (IS_ERR(clk)) {
			pr_warn("Cannot get clock for CPU %d\n", cpu);
		} else {
			if (clk_is_match(clk, cur_clk))
				cpumask_set_cpu(cpu, cpumask);

			clk_put(clk);
		}
	}
}

static int __init armada_8k_add_opp(struct clk *clk, struct device *cpu_dev,
				    struct freq_table *freq_tables,
				    int opps_index)
{
	unsigned int cur_frequency;
	unsigned int freq;
	int i, ret;

	/* Get nominal (current) CPU frequency. */
	cur_frequency = clk_get_rate(clk);
	if (!cur_frequency) {
		dev_err(cpu_dev, "Failed to get clock rate for this CPU\n");
		return -EINVAL;
	}

	freq_tables[opps_index].cpu_dev = cpu_dev;

	for (i = 0; i < ARRAY_SIZE(opps_div); i++) {
		freq = cur_frequency / opps_div[i];

		ret = dev_pm_opp_add(cpu_dev, freq, 0);
		if (ret)
			return ret;

		freq_tables[opps_index].freq[i] = freq;
	}

	return 0;
}

static void armada_8k_cpufreq_free_table(struct freq_table *freq_tables)
{
	int opps_index, nb_cpus = num_possible_cpus();

	for (opps_index = 0 ; opps_index <= nb_cpus; opps_index++) {
		int i;

		/* If cpu_dev is NULL then we reached the end of the array */
		if (!freq_tables[opps_index].cpu_dev)
			break;

		for (i = 0; i < ARRAY_SIZE(opps_div); i++) {
			/*
			 * A 0Hz frequency is not valid, this meant
			 * that it was not yet initialized so there is
			 * no more opp to free
			 */
			if (freq_tables[opps_index].freq[i] == 0)
				break;

			dev_pm_opp_remove(freq_tables[opps_index].cpu_dev,
					  freq_tables[opps_index].freq[i]);
		}
	}

	kfree(freq_tables);
}

static int __init armada_8k_cpufreq_init(void)
{
	int ret = 0, opps_index = 0, cpu, nb_cpus;
	struct freq_table *freq_tables;
	struct device_node *node;
	struct cpumask cpus;

	node = of_find_compatible_node(NULL, NULL, "marvell,ap806-cpu-clock");
	if (!node || !of_device_is_available(node)) {
		of_node_put(node);
		return -ENODEV;
	}

	nb_cpus = num_possible_cpus();
	freq_tables = kcalloc(nb_cpus, sizeof(*freq_tables), GFP_KERNEL);
	cpumask_copy(&cpus, cpu_possible_mask);

	/*
	 * For each CPU, this loop registers the operating points
	 * supported (which are the nominal CPU frequency and full integer
	 * divisions of it).
	 */
	for_each_cpu(cpu, &cpus) {
		struct cpumask shared_cpus;
		struct device *cpu_dev;
		struct clk *clk;

		cpu_dev = get_cpu_device(cpu);

		if (!cpu_dev) {
			pr_err("Cannot get CPU %d\n", cpu);
			continue;
		}

		clk = clk_get(cpu_dev, 0);

		if (IS_ERR(clk)) {
			pr_err("Cannot get clock for CPU %d\n", cpu);
			ret = PTR_ERR(clk);
			goto remove_opp;
		}

		ret = armada_8k_add_opp(clk, cpu_dev, freq_tables, opps_index);
		if (ret) {
			clk_put(clk);
			goto remove_opp;
		}

		opps_index++;
		cpumask_clear(&shared_cpus);
		armada_8k_get_sharing_cpus(clk, &shared_cpus);
		dev_pm_opp_set_sharing_cpus(cpu_dev, &shared_cpus);
		cpumask_andnot(&cpus, &cpus, &shared_cpus);
		clk_put(clk);
	}

	armada_8k_pdev = platform_device_register_simple("cpufreq-dt", -1,
							 NULL, 0);
	ret = PTR_ERR_OR_ZERO(armada_8k_pdev);
	if (ret)
		goto remove_opp;

	platform_set_drvdata(armada_8k_pdev, freq_tables);

	return 0;

remove_opp:
	armada_8k_cpufreq_free_table(freq_tables);
	return ret;
}
module_init(armada_8k_cpufreq_init);

static void __exit armada_8k_cpufreq_exit(void)
{
	struct freq_table *freq_tables = platform_get_drvdata(armada_8k_pdev);

	platform_device_unregister(armada_8k_pdev);
	armada_8k_cpufreq_free_table(freq_tables);
}
module_exit(armada_8k_cpufreq_exit);

MODULE_AUTHOR("Gregory Clement <gregory.clement@bootlin.com>");
MODULE_DESCRIPTION("Armada 8K cpufreq driver");
MODULE_LICENSE("GPL");
