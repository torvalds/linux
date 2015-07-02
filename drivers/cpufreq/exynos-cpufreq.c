/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS - CPU frequency scaling support for EXYNOS series
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/cpufreq.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/cpu_cooling.h>
#include <linux/cpu.h>

#include "exynos-cpufreq.h"

static struct exynos_dvfs_info *exynos_info;
static struct thermal_cooling_device *cdev;
static struct regulator *arm_regulator;
static unsigned int locking_frequency;

static int exynos_cpufreq_get_index(unsigned int freq)
{
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;
	struct cpufreq_frequency_table *pos;

	cpufreq_for_each_entry(pos, freq_table)
		if (pos->frequency == freq)
			break;

	if (pos->frequency == CPUFREQ_TABLE_END)
		return -EINVAL;

	return pos - freq_table;
}

static int exynos_cpufreq_scale(unsigned int target_freq)
{
	struct cpufreq_frequency_table *freq_table = exynos_info->freq_table;
	unsigned int *volt_table = exynos_info->volt_table;
	struct cpufreq_policy *policy = cpufreq_cpu_get(0);
	unsigned int arm_volt, safe_arm_volt = 0;
	unsigned int mpll_freq_khz = exynos_info->mpll_freq_khz;
	struct device *dev = exynos_info->dev;
	unsigned int old_freq;
	int index, old_index;
	int ret = 0;

	old_freq = policy->cur;

	/*
	 * The policy max have been changed so that we cannot get proper
	 * old_index with cpufreq_frequency_table_target(). Thus, ignore
	 * policy and get the index from the raw frequency table.
	 */
	old_index = exynos_cpufreq_get_index(old_freq);
	if (old_index < 0) {
		ret = old_index;
		goto out;
	}

	index = exynos_cpufreq_get_index(target_freq);
	if (index < 0) {
		ret = index;
		goto out;
	}

	/*
	 * ARM clock source will be changed APLL to MPLL temporary
	 * To support this level, need to control regulator for
	 * required voltage level
	 */
	if (exynos_info->need_apll_change != NULL) {
		if (exynos_info->need_apll_change(old_index, index) &&
		   (freq_table[index].frequency < mpll_freq_khz) &&
		   (freq_table[old_index].frequency < mpll_freq_khz))
			safe_arm_volt = volt_table[exynos_info->pll_safe_idx];
	}
	arm_volt = volt_table[index];

	/* When the new frequency is higher than current frequency */
	if ((target_freq > old_freq) && !safe_arm_volt) {
		/* Firstly, voltage up to increase frequency */
		ret = regulator_set_voltage(arm_regulator, arm_volt, arm_volt);
		if (ret) {
			dev_err(dev, "failed to set cpu voltage to %d\n",
				arm_volt);
			return ret;
		}
	}

	if (safe_arm_volt) {
		ret = regulator_set_voltage(arm_regulator, safe_arm_volt,
				      safe_arm_volt);
		if (ret) {
			dev_err(dev, "failed to set cpu voltage to %d\n",
				safe_arm_volt);
			return ret;
		}
	}

	exynos_info->set_freq(old_index, index);

	/* When the new frequency is lower than current frequency */
	if ((target_freq < old_freq) ||
	   ((target_freq > old_freq) && safe_arm_volt)) {
		/* down the voltage after frequency change */
		ret = regulator_set_voltage(arm_regulator, arm_volt,
				arm_volt);
		if (ret) {
			dev_err(dev, "failed to set cpu voltage to %d\n",
				arm_volt);
			goto out;
		}
	}

out:
	cpufreq_cpu_put(policy);

	return ret;
}

static int exynos_target(struct cpufreq_policy *policy, unsigned int index)
{
	return exynos_cpufreq_scale(exynos_info->freq_table[index].frequency);
}

static int exynos_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	policy->clk = exynos_info->cpu_clk;
	policy->suspend_freq = locking_frequency;
	return cpufreq_generic_init(policy, exynos_info->freq_table, 100000);
}

static struct cpufreq_driver exynos_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= exynos_target,
	.get		= cpufreq_generic_get,
	.init		= exynos_cpufreq_cpu_init,
	.name		= "exynos_cpufreq",
	.attr		= cpufreq_generic_attr,
#ifdef CONFIG_ARM_EXYNOS_CPU_FREQ_BOOST_SW
	.boost_supported = true,
#endif
#ifdef CONFIG_PM
	.suspend	= cpufreq_generic_suspend,
#endif
};

static int exynos_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *cpu0;
	int ret = -EINVAL;

	exynos_info = kzalloc(sizeof(*exynos_info), GFP_KERNEL);
	if (!exynos_info)
		return -ENOMEM;

	exynos_info->dev = &pdev->dev;

	if (of_machine_is_compatible("samsung,exynos4212")) {
		exynos_info->type = EXYNOS_SOC_4212;
		ret = exynos4x12_cpufreq_init(exynos_info);
	} else if (of_machine_is_compatible("samsung,exynos4412")) {
		exynos_info->type = EXYNOS_SOC_4412;
		ret = exynos4x12_cpufreq_init(exynos_info);
	} else if (of_machine_is_compatible("samsung,exynos5250")) {
		exynos_info->type = EXYNOS_SOC_5250;
		ret = exynos5250_cpufreq_init(exynos_info);
	} else {
		pr_err("%s: Unknown SoC type\n", __func__);
		return -ENODEV;
	}

	if (ret)
		goto err_vdd_arm;

	if (exynos_info->set_freq == NULL) {
		dev_err(&pdev->dev, "No set_freq function (ERR)\n");
		goto err_vdd_arm;
	}

	arm_regulator = regulator_get(NULL, "vdd_arm");
	if (IS_ERR(arm_regulator)) {
		dev_err(&pdev->dev, "failed to get resource vdd_arm\n");
		goto err_vdd_arm;
	}

	/* Done here as we want to capture boot frequency */
	locking_frequency = clk_get_rate(exynos_info->cpu_clk) / 1000;

	ret = cpufreq_register_driver(&exynos_driver);
	if (ret)
		goto err_cpufreq_reg;

	cpu0 = of_get_cpu_node(0, NULL);
	if (!cpu0) {
		pr_err("failed to find cpu0 node\n");
		return 0;
	}

	if (of_find_property(cpu0, "#cooling-cells", NULL)) {
		cdev = of_cpufreq_cooling_register(cpu0,
						   cpu_present_mask);
		if (IS_ERR(cdev))
			pr_err("running cpufreq without cooling device: %ld\n",
			       PTR_ERR(cdev));
	}

	return 0;

err_cpufreq_reg:
	dev_err(&pdev->dev, "failed to register cpufreq driver\n");
	regulator_put(arm_regulator);
err_vdd_arm:
	kfree(exynos_info);
	return -EINVAL;
}

static struct platform_driver exynos_cpufreq_platdrv = {
	.driver = {
		.name	= "exynos-cpufreq",
	},
	.probe = exynos_cpufreq_probe,
};
module_platform_driver(exynos_cpufreq_platdrv);
