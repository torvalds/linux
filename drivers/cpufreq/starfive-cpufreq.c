// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 StarFive Technology Co., Ltd.
 *
 * Starfive CPUfreq Support
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>

#define VOLT_TOL		(10000)

struct starfive_cpu_dvfs_info {
	struct regulator *vddcpu;
	struct clk *cpu_clk;
	unsigned long regulator_latency;
	struct device *cpu_dev;
	struct cpumask cpus;
};

static int starfive_cpufreq_set_target_index(struct cpufreq_policy *policy,
					unsigned int index)
{
	struct cpufreq_frequency_table *freq_table = policy->freq_table;
	struct starfive_cpu_dvfs_info *info = cpufreq_get_driver_data();
	struct dev_pm_opp *opp;
	unsigned long old_freq, new_freq;
	int old_vdd, target_vdd, ret;

	old_freq = clk_get_rate(info->cpu_clk);
	old_vdd = regulator_get_voltage(info->vddcpu);
	if (old_vdd < 0) {
		pr_err("Invalid cpu regulator value: %d\n", old_vdd);
		return old_vdd;
	}

	new_freq = freq_table[index].frequency * 1000;
	opp = dev_pm_opp_find_freq_ceil(info->cpu_dev, &new_freq);
	if (IS_ERR(opp)) {
		pr_err("Failed to find OPP for %ld\n", new_freq);
		return PTR_ERR(opp);
	}
	target_vdd = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);


	if (info->vddcpu && new_freq > old_freq) {
		ret = regulator_set_voltage(info->vddcpu,
					   target_vdd, target_vdd + VOLT_TOL);
		if (ret != 0) {
			pr_err("Failed to set vddcpu for %ldkHz: %d\n",
			       new_freq, ret);
			return ret;
		}
	}

	ret = clk_set_rate(info->cpu_clk, new_freq);
	if (ret < 0) {
		pr_err("Failed to set rate %ldkHz: %d\n",
		       new_freq, ret);
	}

	if (info->vddcpu && new_freq < old_freq) {
		ret = regulator_set_voltage(info->vddcpu,
					    target_vdd, target_vdd + VOLT_TOL);
		if (ret != 0) {
			pr_err("Failed to set vddcpu for %ldkHz: %d\n",
			       new_freq, ret);
			if (clk_set_rate(policy->clk, old_freq * 1000) < 0)
				pr_err("Failed to restore original clock rate\n");

			return ret;
		}
	}

	pr_debug("Set actual frequency %lukHz\n",
		 clk_get_rate(policy->clk) / 1000);

	return 0;
}

static int starfive_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	struct starfive_cpu_dvfs_info *info = cpufreq_get_driver_data();
	struct cpufreq_frequency_table *freq_table;
	int ret;

	ret = dev_pm_opp_init_cpufreq_table(info->cpu_dev, &freq_table);
	if (ret) {
		pr_err("Failed to init cpufreq table for cpu%d: %d\n",
		       policy->cpu, ret);
		return ret;
	}

	cpumask_copy(policy->cpus, &info->cpus);
	policy->freq_table = freq_table;
	policy->driver_data = info;
	policy->clk = info->cpu_clk;

	return 0;
}

static int starfive_cpu_dvfs_info_init(struct platform_device *pdev,
			struct starfive_cpu_dvfs_info *info)
{
	struct device *dev = &pdev->dev;
	int ret;
	static int retry = 3;

	info->vddcpu = regulator_get_optional(&pdev->dev, "cpu_vdd_0p9");
	if (IS_ERR(info->vddcpu)) {
		if (PTR_ERR(info->vddcpu) == -EPROBE_DEFER)
			dev_warn(&pdev->dev, "The cpu regulator is not ready, retry.\n");
		else
			dev_err(&pdev->dev, "Failed to get regulator for cpu!\n");
		if (retry-- > 0)
			return -EPROBE_DEFER;
		else
			return PTR_ERR(info->vddcpu);
	}

	info->cpu_clk = devm_clk_get(dev, "cpu_clk");
	if (IS_ERR(info->cpu_clk)) {
		dev_err(&pdev->dev, "Unable to obtain cpu_clk: %ld\n",
			   PTR_ERR(info->cpu_clk));
		return PTR_ERR(info->cpu_clk);
	}

	info->cpu_dev = get_cpu_device(1);
	if (!info->cpu_dev) {
		dev_err(&pdev->dev, "Failed to get cpu device\n");
		return -ENODEV;
	}
	/* Get OPP-sharing information from "operating-points-v2" bindings */
	ret = dev_pm_opp_of_get_sharing_cpus(info->cpu_dev, &info->cpus);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get OPP-sharing information for cpu\n");
		return -EINVAL;
	}

	ret = dev_pm_opp_of_cpumask_add_table(&info->cpus);
	if (ret) {
		pr_warn("no OPP table for cpu\n");
		return -EINVAL;
	}

	return 0;
}

static struct cpufreq_driver starfive_cpufreq_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= starfive_cpufreq_set_target_index,
	.get		= cpufreq_generic_get,
	.init		= starfive_cpufreq_driver_init,
	.name		= "starfive-cpufreq",
	.attr		= cpufreq_generic_attr,
};

static int starfive_cpufreq_probe(struct platform_device *pdev)
{
	struct starfive_cpu_dvfs_info *info;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	ret = starfive_cpu_dvfs_info_init(pdev, info);
	if (ret) {
		dev_err(&pdev->dev, "Failed to init starfive cpu dvfs info\n");
		return ret;
	}

	starfive_cpufreq_driver.driver_data = info;
	ret = cpufreq_register_driver(&starfive_cpufreq_driver);
	if (ret)
		dev_err(&pdev->dev, "Failed to register starfive cpufreq driver\n");

	return ret;

}

static const struct of_device_id starfive_cpufreq_match_table[] = {
	{ .compatible = "starfive,jh7110-cpufreq" },
	{}
};

static struct platform_driver starfive_cpufreq_plat_driver = {
	.probe = starfive_cpufreq_probe,
	.driver = {
		.name = "starfive-cpufreq",
		.of_match_table = starfive_cpufreq_match_table,
	},
};

static int __init starfive_cpufreq_init(void)
{
	return platform_driver_register(&starfive_cpufreq_plat_driver);
}
device_initcall(starfive_cpufreq_init);

MODULE_DESCRIPTION("STARFIVE CPUFREQ Driver");
MODULE_AUTHOR("Mason Huuo <mason.huo@starfivetech.com>");
MODULE_LICENSE("GPL v2");

