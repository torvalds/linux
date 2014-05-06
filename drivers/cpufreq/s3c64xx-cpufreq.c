/*
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * S3C64xx CPUfreq Support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "cpufreq: " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>

static struct regulator *vddarm;
static unsigned long regulator_latency;

#ifdef CONFIG_CPU_S3C6410
struct s3c64xx_dvfs {
	unsigned int vddarm_min;
	unsigned int vddarm_max;
};

static struct s3c64xx_dvfs s3c64xx_dvfs_table[] = {
	[0] = { 1000000, 1150000 },
	[1] = { 1050000, 1150000 },
	[2] = { 1100000, 1150000 },
	[3] = { 1200000, 1350000 },
	[4] = { 1300000, 1350000 },
};

static struct cpufreq_frequency_table s3c64xx_freq_table[] = {
	{ 0, 0,  66000 },
	{ 0, 0, 100000 },
	{ 0, 0, 133000 },
	{ 0, 1, 200000 },
	{ 0, 1, 222000 },
	{ 0, 1, 266000 },
	{ 0, 2, 333000 },
	{ 0, 2, 400000 },
	{ 0, 2, 532000 },
	{ 0, 2, 533000 },
	{ 0, 3, 667000 },
	{ 0, 4, 800000 },
	{ 0, 0, CPUFREQ_TABLE_END },
};
#endif

static int s3c64xx_cpufreq_set_target(struct cpufreq_policy *policy,
				      unsigned int index)
{
	struct s3c64xx_dvfs *dvfs;
	unsigned int old_freq, new_freq;
	int ret;

	old_freq = clk_get_rate(policy->clk) / 1000;
	new_freq = s3c64xx_freq_table[index].frequency;
	dvfs = &s3c64xx_dvfs_table[s3c64xx_freq_table[index].driver_data];

#ifdef CONFIG_REGULATOR
	if (vddarm && new_freq > old_freq) {
		ret = regulator_set_voltage(vddarm,
					    dvfs->vddarm_min,
					    dvfs->vddarm_max);
		if (ret != 0) {
			pr_err("Failed to set VDDARM for %dkHz: %d\n",
			       new_freq, ret);
			return ret;
		}
	}
#endif

	ret = clk_set_rate(policy->clk, new_freq * 1000);
	if (ret < 0) {
		pr_err("Failed to set rate %dkHz: %d\n",
		       new_freq, ret);
		return ret;
	}

#ifdef CONFIG_REGULATOR
	if (vddarm && new_freq < old_freq) {
		ret = regulator_set_voltage(vddarm,
					    dvfs->vddarm_min,
					    dvfs->vddarm_max);
		if (ret != 0) {
			pr_err("Failed to set VDDARM for %dkHz: %d\n",
			       new_freq, ret);
			if (clk_set_rate(policy->clk, old_freq * 1000) < 0)
				pr_err("Failed to restore original clock rate\n");

			return ret;
		}
	}
#endif

	pr_debug("Set actual frequency %lukHz\n",
		 clk_get_rate(policy->clk) / 1000);

	return 0;
}

#ifdef CONFIG_REGULATOR
static void __init s3c64xx_cpufreq_config_regulator(void)
{
	int count, v, i, found;
	struct cpufreq_frequency_table *freq;
	struct s3c64xx_dvfs *dvfs;

	count = regulator_count_voltages(vddarm);
	if (count < 0) {
		pr_err("Unable to check supported voltages\n");
	}

	freq = s3c64xx_freq_table;
	while (count > 0 && freq->frequency != CPUFREQ_TABLE_END) {
		if (freq->frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		dvfs = &s3c64xx_dvfs_table[freq->driver_data];
		found = 0;

		for (i = 0; i < count; i++) {
			v = regulator_list_voltage(vddarm, i);
			if (v >= dvfs->vddarm_min && v <= dvfs->vddarm_max)
				found = 1;
		}

		if (!found) {
			pr_debug("%dkHz unsupported by regulator\n",
				 freq->frequency);
			freq->frequency = CPUFREQ_ENTRY_INVALID;
		}

		freq++;
	}

	/* Guess based on having to do an I2C/SPI write; in future we
	 * will be able to query the regulator performance here. */
	regulator_latency = 1 * 1000 * 1000;
}
#endif

static int s3c64xx_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	int ret;
	struct cpufreq_frequency_table *freq;

	if (policy->cpu != 0)
		return -EINVAL;

	if (s3c64xx_freq_table == NULL) {
		pr_err("No frequency information for this CPU\n");
		return -ENODEV;
	}

	policy->clk = clk_get(NULL, "armclk");
	if (IS_ERR(policy->clk)) {
		pr_err("Unable to obtain ARMCLK: %ld\n",
		       PTR_ERR(policy->clk));
		return PTR_ERR(policy->clk);
	}

#ifdef CONFIG_REGULATOR
	vddarm = regulator_get(NULL, "vddarm");
	if (IS_ERR(vddarm)) {
		ret = PTR_ERR(vddarm);
		pr_err("Failed to obtain VDDARM: %d\n", ret);
		pr_err("Only frequency scaling available\n");
		vddarm = NULL;
	} else {
		s3c64xx_cpufreq_config_regulator();
	}
#endif

	freq = s3c64xx_freq_table;
	while (freq->frequency != CPUFREQ_TABLE_END) {
		unsigned long r;

		/* Check for frequencies we can generate */
		r = clk_round_rate(policy->clk, freq->frequency * 1000);
		r /= 1000;
		if (r != freq->frequency) {
			pr_debug("%dkHz unsupported by clock\n",
				 freq->frequency);
			freq->frequency = CPUFREQ_ENTRY_INVALID;
		}

		/* If we have no regulator then assume startup
		 * frequency is the maximum we can support. */
		if (!vddarm && freq->frequency > clk_get_rate(policy->clk) / 1000)
			freq->frequency = CPUFREQ_ENTRY_INVALID;

		freq++;
	}

	/* Datasheet says PLL stabalisation time (if we were to use
	 * the PLLs, which we don't currently) is ~300us worst case,
	 * but add some fudge.
	 */
	ret = cpufreq_generic_init(policy, s3c64xx_freq_table,
			(500 * 1000) + regulator_latency);
	if (ret != 0) {
		pr_err("Failed to configure frequency table: %d\n",
		       ret);
		regulator_put(vddarm);
		clk_put(policy->clk);
	}

	return ret;
}

static struct cpufreq_driver s3c64xx_cpufreq_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= s3c64xx_cpufreq_set_target,
	.get		= cpufreq_generic_get,
	.init		= s3c64xx_cpufreq_driver_init,
	.name		= "s3c",
};

static int __init s3c64xx_cpufreq_init(void)
{
	return cpufreq_register_driver(&s3c64xx_cpufreq_driver);
}
module_init(s3c64xx_cpufreq_init);
