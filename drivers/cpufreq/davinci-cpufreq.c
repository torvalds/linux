// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU frequency scaling for DaVinci
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Based on linux/arch/arm/plat-omap/cpu-omap.c. Original Copyright follows:
 *
 *  Copyright (C) 2005 Nokia Corporation
 *  Written by Tony Lindgren <tony@atomide.com>
 *
 *  Based on cpu-sa1110.c, Copyright (C) 2001 Russell King
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 * Updated to support OMAP3
 * Rajendra Nayak <rnayak@ti.com>
 */
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_data/davinci-cpufreq.h>
#include <linux/platform_device.h>
#include <linux/export.h>

struct davinci_cpufreq {
	struct device *dev;
	struct clk *armclk;
	struct clk *asyncclk;
	unsigned long asyncrate;
};
static struct davinci_cpufreq cpufreq;

static int davinci_target(struct cpufreq_policy *policy, unsigned int idx)
{
	struct davinci_cpufreq_config *pdata = cpufreq.dev->platform_data;
	struct clk *armclk = cpufreq.armclk;
	unsigned int old_freq, new_freq;
	int ret = 0;

	old_freq = policy->cur;
	new_freq = pdata->freq_table[idx].frequency;

	/* if moving to higher frequency, up the voltage beforehand */
	if (pdata->set_voltage && new_freq > old_freq) {
		ret = pdata->set_voltage(idx);
		if (ret)
			return ret;
	}

	ret = clk_set_rate(armclk, new_freq * 1000);
	if (ret)
		return ret;

	if (cpufreq.asyncclk) {
		ret = clk_set_rate(cpufreq.asyncclk, cpufreq.asyncrate);
		if (ret)
			return ret;
	}

	/* if moving to lower freq, lower the voltage after lowering freq */
	if (pdata->set_voltage && new_freq < old_freq)
		pdata->set_voltage(idx);

	return 0;
}

static int davinci_cpu_init(struct cpufreq_policy *policy)
{
	int result = 0;
	struct davinci_cpufreq_config *pdata = cpufreq.dev->platform_data;
	struct cpufreq_frequency_table *freq_table = pdata->freq_table;

	if (policy->cpu != 0)
		return -EINVAL;

	/* Finish platform specific initialization */
	if (pdata->init) {
		result = pdata->init();
		if (result)
			return result;
	}

	policy->clk = cpufreq.armclk;

	/*
	 * Time measurement across the target() function yields ~1500-1800us
	 * time taken with no drivers on notification list.
	 * Setting the latency to 2000 us to accommodate addition of drivers
	 * to pre/post change notification list.
	 */
	cpufreq_generic_init(policy, freq_table, 2000 * 1000);
	return 0;
}

static struct cpufreq_driver davinci_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= davinci_target,
	.get		= cpufreq_generic_get,
	.init		= davinci_cpu_init,
	.name		= "davinci",
	.attr		= cpufreq_generic_attr,
};

static int __init davinci_cpufreq_probe(struct platform_device *pdev)
{
	struct davinci_cpufreq_config *pdata = pdev->dev.platform_data;
	struct clk *asyncclk;

	if (!pdata)
		return -EINVAL;
	if (!pdata->freq_table)
		return -EINVAL;

	cpufreq.dev = &pdev->dev;

	cpufreq.armclk = clk_get(NULL, "arm");
	if (IS_ERR(cpufreq.armclk)) {
		dev_err(cpufreq.dev, "Unable to get ARM clock\n");
		return PTR_ERR(cpufreq.armclk);
	}

	asyncclk = clk_get(cpufreq.dev, "async");
	if (!IS_ERR(asyncclk)) {
		cpufreq.asyncclk = asyncclk;
		cpufreq.asyncrate = clk_get_rate(asyncclk);
	}

	return cpufreq_register_driver(&davinci_driver);
}

static int __exit davinci_cpufreq_remove(struct platform_device *pdev)
{
	cpufreq_unregister_driver(&davinci_driver);

	clk_put(cpufreq.armclk);

	if (cpufreq.asyncclk)
		clk_put(cpufreq.asyncclk);

	return 0;
}

static struct platform_driver davinci_cpufreq_driver = {
	.driver = {
		.name	 = "cpufreq-davinci",
	},
	.remove = __exit_p(davinci_cpufreq_remove),
};

int __init davinci_cpufreq_init(void)
{
	return platform_driver_probe(&davinci_cpufreq_driver,
							davinci_cpufreq_probe);
}

