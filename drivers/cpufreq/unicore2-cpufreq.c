// SPDX-License-Identifier: GPL-2.0-only
/*
 * clock scaling for the UniCore-II
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 */

#include <linux/err.h>
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
static int ucv2_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu)
		return -EINVAL;

	cpufreq_verify_within_cpu_limits(policy);
	return 0;
}

static int ucv2_target(struct cpufreq_policy *policy,
			 unsigned int target_freq,
			 unsigned int relation)
{
	struct cpufreq_freqs freqs;
	int ret;

	freqs.old = policy->cur;
	freqs.new = target_freq;

	cpufreq_freq_transition_begin(policy, &freqs);
	ret = clk_set_rate(policy->clk, target_freq * 1000);
	cpufreq_freq_transition_end(policy, &freqs, ret);

	return ret;
}

static int __init ucv2_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	policy->min = policy->cpuinfo.min_freq = 250000;
	policy->max = policy->cpuinfo.max_freq = 1000000;
	policy->clk = clk_get(NULL, "MAIN_CLK");
	return PTR_ERR_OR_ZERO(policy->clk);
}

static struct cpufreq_driver ucv2_driver = {
	.flags		= CPUFREQ_STICKY | CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING,
	.verify		= ucv2_verify_speed,
	.target		= ucv2_target,
	.get		= cpufreq_generic_get,
	.init		= ucv2_cpu_init,
	.name		= "UniCore-II",
};

static int __init ucv2_cpufreq_init(void)
{
	return cpufreq_register_driver(&ucv2_driver);
}

arch_initcall(ucv2_cpufreq_init);
