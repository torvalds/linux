/*
 *  linux/arch/arm/plat-omap/cpu-omap.c
 *
 *  CPU frequency scaling for OMAP
 *
 *  Copyright (C) 2005 Nokia Corporation
 *  Written by Tony Lindgren <tony@atomide.com>
 *
 *  Based on cpu-sa1110.c, Copyright (C) 2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/system.h>

#define VERY_HI_RATE	900000000

#ifdef CONFIG_ARCH_OMAP1
#define MPU_CLK		"mpu"
#else
#define MPU_CLK		"virt_prcm_set"
#endif

/* TODO: Add support for SDRAM timing changes */

int omap_verify_speed(struct cpufreq_policy *policy)
{
	struct clk * mpu_clk;

	if (policy->cpu)
		return -EINVAL;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);
	mpu_clk = clk_get(NULL, MPU_CLK);
	if (IS_ERR(mpu_clk))
		return PTR_ERR(mpu_clk);
	policy->min = clk_round_rate(mpu_clk, policy->min * 1000) / 1000;
	policy->max = clk_round_rate(mpu_clk, policy->max * 1000) / 1000;
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);
	clk_put(mpu_clk);

	return 0;
}

unsigned int omap_getspeed(unsigned int cpu)
{
	struct clk * mpu_clk;
	unsigned long rate;

	if (cpu)
		return 0;

	mpu_clk = clk_get(NULL, MPU_CLK);
	if (IS_ERR(mpu_clk))
		return 0;
	rate = clk_get_rate(mpu_clk) / 1000;
	clk_put(mpu_clk);

	return rate;
}

static int omap_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	struct clk * mpu_clk;
	struct cpufreq_freqs freqs;
	int ret = 0;

	mpu_clk = clk_get(NULL, MPU_CLK);
	if (IS_ERR(mpu_clk))
		return PTR_ERR(mpu_clk);

	freqs.old = omap_getspeed(0);
	freqs.new = clk_round_rate(mpu_clk, target_freq * 1000) / 1000;
	freqs.cpu = 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	ret = clk_set_rate(mpu_clk, target_freq * 1000);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	clk_put(mpu_clk);

	return ret;
}

static int __init omap_cpu_init(struct cpufreq_policy *policy)
{
	struct clk * mpu_clk;

	mpu_clk = clk_get(NULL, MPU_CLK);
	if (IS_ERR(mpu_clk))
		return PTR_ERR(mpu_clk);

	if (policy->cpu != 0)
		return -EINVAL;
	policy->cur = policy->min = policy->max = omap_getspeed(0);
	policy->cpuinfo.min_freq = clk_round_rate(mpu_clk, 0) / 1000;
	policy->cpuinfo.max_freq = clk_round_rate(mpu_clk, VERY_HI_RATE) / 1000;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	clk_put(mpu_clk);

	return 0;
}

static struct cpufreq_driver omap_driver = {
	.flags		= CPUFREQ_STICKY,
	.verify		= omap_verify_speed,
	.target		= omap_target,
	.get		= omap_getspeed,
	.init		= omap_cpu_init,
	.name		= "omap",
};

static int __init omap_cpufreq_init(void)
{
	return cpufreq_register_driver(&omap_driver);
}

arch_initcall(omap_cpufreq_init);
