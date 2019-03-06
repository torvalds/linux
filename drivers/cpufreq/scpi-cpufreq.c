/*
 * System Control and Power Interface (SCPI) based CPUFreq Interface driver
 *
 * It provides necessary ops to arm_big_little cpufreq driver.
 *
 * Copyright (C) 2015 ARM Ltd.
 * Sudeep Holla <sudeep.holla@arm.com>
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

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/scpi_protocol.h>
#include <linux/slab.h>
#include <linux/types.h>

struct scpi_data {
	struct clk *clk;
	struct device *cpu_dev;
};

static struct scpi_ops *scpi_ops;

static unsigned int scpi_cpufreq_get_rate(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);
	struct scpi_data *priv = policy->driver_data;
	unsigned long rate = clk_get_rate(priv->clk);

	return rate / 1000;
}

static int
scpi_cpufreq_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned long freq = policy->freq_table[index].frequency;
	struct scpi_data *priv = policy->driver_data;
	u64 rate = freq * 1000;
	int ret;

	ret = clk_set_rate(priv->clk, rate);

	if (ret)
		return ret;

	if (clk_get_rate(priv->clk) != rate)
		return -EIO;

	arch_set_freq_scale(policy->related_cpus, freq,
			    policy->cpuinfo.max_freq);

	return 0;
}

static int
scpi_get_sharing_cpus(struct device *cpu_dev, struct cpumask *cpumask)
{
	int cpu, domain, tdomain;
	struct device *tcpu_dev;

	domain = scpi_ops->device_domain_id(cpu_dev);
	if (domain < 0)
		return domain;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev)
			continue;

		tdomain = scpi_ops->device_domain_id(tcpu_dev);
		if (tdomain == domain)
			cpumask_set_cpu(cpu, cpumask);
	}

	return 0;
}

static int scpi_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret;
	unsigned int latency;
	struct device *cpu_dev;
	struct scpi_data *priv;
	struct cpufreq_frequency_table *freq_table;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("failed to get cpu%d device\n", policy->cpu);
		return -ENODEV;
	}

	ret = scpi_ops->add_opps_to_device(cpu_dev);
	if (ret) {
		dev_warn(cpu_dev, "failed to add opps to the device\n");
		return ret;
	}

	ret = scpi_get_sharing_cpus(cpu_dev, policy->cpus);
	if (ret) {
		dev_warn(cpu_dev, "failed to get sharing cpumask\n");
		return ret;
	}

	ret = dev_pm_opp_set_sharing_cpus(cpu_dev, policy->cpus);
	if (ret) {
		dev_err(cpu_dev, "%s: failed to mark OPPs as shared: %d\n",
			__func__, ret);
		return ret;
	}

	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_dbg(cpu_dev, "OPP table is not ready, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto out_free_opp;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out_free_opp;
	}

	ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
	if (ret) {
		dev_err(cpu_dev, "failed to init cpufreq table: %d\n", ret);
		goto out_free_priv;
	}

	priv->cpu_dev = cpu_dev;
	priv->clk = clk_get(cpu_dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(cpu_dev, "%s: Failed to get clk for cpu: %d\n",
			__func__, cpu_dev->id);
		ret = PTR_ERR(priv->clk);
		goto out_free_cpufreq_table;
	}

	policy->driver_data = priv;
	policy->freq_table = freq_table;

	/* scpi allows DVFS request for any domain from any CPU */
	policy->dvfs_possible_from_any_cpu = true;

	latency = scpi_ops->get_transition_latency(cpu_dev);
	if (!latency)
		latency = CPUFREQ_ETERNAL;

	policy->cpuinfo.transition_latency = latency;

	policy->fast_switch_possible = false;

	dev_pm_opp_of_register_em(policy->cpus);

	return 0;

out_free_cpufreq_table:
	dev_pm_opp_free_cpufreq_table(cpu_dev, &freq_table);
out_free_priv:
	kfree(priv);
out_free_opp:
	dev_pm_opp_remove_all_dynamic(cpu_dev);

	return ret;
}

static int scpi_cpufreq_exit(struct cpufreq_policy *policy)
{
	struct scpi_data *priv = policy->driver_data;

	clk_put(priv->clk);
	dev_pm_opp_free_cpufreq_table(priv->cpu_dev, &policy->freq_table);
	kfree(priv);
	dev_pm_opp_remove_all_dynamic(priv->cpu_dev);

	return 0;
}

static struct cpufreq_driver scpi_cpufreq_driver = {
	.name	= "scpi-cpufreq",
	.flags	= CPUFREQ_STICKY | CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
		  CPUFREQ_NEED_INITIAL_FREQ_CHECK |
		  CPUFREQ_IS_COOLING_DEV,
	.verify	= cpufreq_generic_frequency_table_verify,
	.attr	= cpufreq_generic_attr,
	.get	= scpi_cpufreq_get_rate,
	.init	= scpi_cpufreq_init,
	.exit	= scpi_cpufreq_exit,
	.target_index	= scpi_cpufreq_set_target,
};

static int scpi_cpufreq_probe(struct platform_device *pdev)
{
	int ret;

	scpi_ops = get_scpi_ops();
	if (!scpi_ops)
		return -EIO;

	ret = cpufreq_register_driver(&scpi_cpufreq_driver);
	if (ret)
		dev_err(&pdev->dev, "%s: registering cpufreq failed, err: %d\n",
			__func__, ret);
	return ret;
}

static int scpi_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&scpi_cpufreq_driver);
	scpi_ops = NULL;
	return 0;
}

static struct platform_driver scpi_cpufreq_platdrv = {
	.driver = {
		.name	= "scpi-cpufreq",
	},
	.probe		= scpi_cpufreq_probe,
	.remove		= scpi_cpufreq_remove,
};
module_platform_driver(scpi_cpufreq_platdrv);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("ARM SCPI CPUFreq interface driver");
MODULE_LICENSE("GPL v2");
