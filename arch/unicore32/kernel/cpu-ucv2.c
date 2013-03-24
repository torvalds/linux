/*
 * linux/arch/unicore32/kernel/cpu-ucv2.c: clock scaling for the UniCore-II
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include <mach/hardware.h>

static struct cpufreq_driver ucv2_driver;

/* make sure that only the "userspace" governor is run
 * -- anything else wouldn't make sense on this platform, anyway.
 */
int ucv2_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu)
		return -EINVAL;

	cpufreq_verify_within_limits(policy,
			policy->cpuinfo.min_freq, policy->cpuinfo.max_freq);

	return 0;
}

static unsigned int ucv2_getspeed(unsigned int cpu)
{
	struct clk *mclk = clk_get(NULL, "MAIN_CLK");

	if (cpu)
		return 0;
	return clk_get_rate(mclk)/1000;
}

static int ucv2_target(struct cpufreq_policy *policy,
			 unsigned int target_freq,
			 unsigned int relation)
{
	unsigned int cur = ucv2_getspeed(0);
	struct cpufreq_freqs freqs;
	struct clk *mclk = clk_get(NULL, "MAIN_CLK");

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	if (!clk_set_rate(mclk, target_freq * 1000)) {
		freqs.old = cur;
		freqs.new = target_freq;
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int __init ucv2_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;
	policy->cur = ucv2_getspeed(0);
	policy->min = policy->cpuinfo.min_freq = 250000;
	policy->max = policy->cpuinfo.max_freq = 1000000;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	return 0;
}

static struct cpufreq_driver ucv2_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= ucv2_verify_speed,
	.target		= ucv2_target,
	.get		= ucv2_getspeed,
	.init		= ucv2_cpu_init,
	.name		= "UniCore-II",
};

static int __init ucv2_cpufreq_init(void)
{
	return cpufreq_register_driver(&ucv2_driver);
}

arch_initcall(ucv2_cpufreq_init);
