/*
 * CPU frequency scaling for DaVinci
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <mach/cpufreq.h>
#include <mach/common.h>

#include "clock.h"

struct davinci_cpufreq {
	struct device *dev;
	struct clk *armclk;
	struct clk *asyncclk;
	unsigned long asyncrate;
};
static struct davinci_cpufreq cpufreq;

static int davinci_verify_speed(struct cpufreq_policy *policy)
{
	struct davinci_cpufreq_config *pdata = cpufreq.dev->platform_data;
	struct cpufreq_frequency_table *freq_table = pdata->freq_table;
	struct clk *armclk = cpufreq.armclk;

	if (freq_table)
		return cpufreq_frequency_table_verify(policy, freq_table);

	if (policy->cpu)
		return -EINVAL;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	policy->min = clk_round_rate(armclk, policy->min * 1000) / 1000;
	policy->max = clk_round_rate(armclk, policy->max * 1000) / 1000;
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
						policy->cpuinfo.max_freq);
	return 0;
}

static unsigned int davinci_getspeed(unsigned int cpu)
{
	if (cpu)
		return 0;

	return clk_get_rate(cpufreq.armclk) / 1000;
}

static int davinci_target(struct cpufreq_policy *policy,
				unsigned int target_freq, unsigned int relation)
{
	int ret = 0;
	unsigned int idx;
	struct cpufreq_freqs freqs;
	struct davinci_cpufreq_config *pdata = cpufreq.dev->platform_data;
	struct clk *armclk = cpufreq.armclk;

	/*
	 * Ensure desired rate is within allowed range.  Some govenors
	 * (ondemand) will just pass target_freq=0 to get the minimum.
	 */
	if (target_freq < policy->cpuinfo.min_freq)
		target_freq = policy->cpuinfo.min_freq;
	if (target_freq > policy->cpuinfo.max_freq)
		target_freq = policy->cpuinfo.max_freq;

	freqs.old = davinci_getspeed(0);
	freqs.new = clk_round_rate(armclk, target_freq * 1000) / 1000;
	freqs.cpu = 0;

	if (freqs.old == freqs.new)
		return ret;

	cpufreq_debug_printk(CPUFREQ_DEBUG_DRIVER,
			dev_driver_string(cpufreq.dev),
			"transition: %u --> %u\n", freqs.old, freqs.new);

	ret = cpufreq_frequency_table_target(policy, pdata->freq_table,
						freqs.new, relation, &idx);
	if (ret)
		return -EINVAL;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* if moving to higher frequency, up the voltage beforehand */
	if (pdata->set_voltage && freqs.new > freqs.old) {
		ret = pdata->set_voltage(idx);
		if (ret)
			goto out;
	}

	ret = clk_set_rate(armclk, idx);
	if (ret)
		goto out;

	if (cpufreq.asyncclk) {
		ret = clk_set_rate(cpufreq.asyncclk, cpufreq.asyncrate);
		if (ret)
			goto out;
	}

	/* if moving to lower freq, lower the voltage after lowering freq */
	if (pdata->set_voltage && freqs.new < freqs.old)
		pdata->set_voltage(idx);

out:
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

static int __init davinci_cpu_init(struct cpufreq_policy *policy)
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

	policy->cur = policy->min = policy->max = davinci_getspeed(0);

	if (freq_table) {
		result = cpufreq_frequency_table_cpuinfo(policy, freq_table);
		if (!result)
			cpufreq_frequency_table_get_attr(freq_table,
							policy->cpu);
	} else {
		policy->cpuinfo.min_freq = policy->min;
		policy->cpuinfo.max_freq = policy->max;
	}

	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->cur = davinci_getspeed(0);

	/*
	 * Time measurement across the target() function yields ~1500-1800us
	 * time taken with no drivers on notification list.
	 * Setting the latency to 2000 us to accomodate addition of drivers
	 * to pre/post change notification list.
	 */
	policy->cpuinfo.transition_latency = 2000 * 1000;
	return 0;
}

static int davinci_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct freq_attr *davinci_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver davinci_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= davinci_verify_speed,
	.target		= davinci_target,
	.get		= davinci_getspeed,
	.init		= davinci_cpu_init,
	.exit		= davinci_cpu_exit,
	.name		= "davinci",
	.attr		= davinci_cpufreq_attr,
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
	clk_put(cpufreq.armclk);

	if (cpufreq.asyncclk)
		clk_put(cpufreq.asyncclk);

	return cpufreq_unregister_driver(&davinci_driver);
}

static struct platform_driver davinci_cpufreq_driver = {
	.driver = {
		.name	 = "cpufreq-davinci",
		.owner	 = THIS_MODULE,
	},
	.remove = __exit_p(davinci_cpufreq_remove),
};

static int __init davinci_cpufreq_init(void)
{
	return platform_driver_probe(&davinci_cpufreq_driver,
							davinci_cpufreq_probe);
}
late_initcall(davinci_cpufreq_init);

