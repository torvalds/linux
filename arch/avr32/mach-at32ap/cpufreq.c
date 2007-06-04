/*
 * Copyright (C) 2004-2007 Atmel Corporation
 *
 * Based on MIPS implementation arch/mips/kernel/time.c
 *   Copyright 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/system.h>

static struct clk *cpuclk;

static int at32_verify_speed(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
			policy->cpuinfo.max_freq);
	return 0;
}

static unsigned int at32_get_speed(unsigned int cpu)
{
	/* No SMP support */
	if (cpu)
		return 0;
	return (unsigned int)((clk_get_rate(cpuclk) + 500) / 1000);
}

static int at32_set_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	struct cpufreq_freqs freqs;
	long freq;

	/* Convert target_freq from kHz to Hz */
	freq = clk_round_rate(cpuclk, target_freq * 1000);

	/* Check if policy->min <= new_freq <= policy->max */
	if(freq < (policy->min * 1000) || freq > (policy->max * 1000))
		return -EINVAL;

	pr_debug("cpufreq: requested frequency %u Hz\n", target_freq * 1000);

	freqs.old = at32_get_speed(0);
	freqs.new = (freq + 500) / 1000;
	freqs.cpu = 0;
	freqs.flags = 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	clk_set_rate(cpuclk, freq);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	pr_debug("cpufreq: set frequency %lu Hz\n", freq);

	return 0;
}

static int __init at32_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	if (policy->cpu != 0)
		return -EINVAL;

	cpuclk = clk_get(NULL, "cpu");
	if (IS_ERR(cpuclk)) {
		pr_debug("cpufreq: could not get CPU clk\n");
		return PTR_ERR(cpuclk);
	}

	policy->cpuinfo.min_freq = (clk_round_rate(cpuclk, 1) + 500) / 1000;
	policy->cpuinfo.max_freq = (clk_round_rate(cpuclk, ~0UL) + 500) / 1000;
	policy->cpuinfo.transition_latency = 0;
	policy->cur = at32_get_speed(0);
	policy->min = policy->cpuinfo.min_freq;
	policy->max = policy->cpuinfo.max_freq;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	printk("cpufreq: AT32AP CPU frequency driver\n");

	return 0;
}

static struct cpufreq_driver at32_driver = {
	.name		= "at32ap",
	.owner		= THIS_MODULE,
	.init		= at32_cpufreq_driver_init,
	.verify		= at32_verify_speed,
	.target		= at32_set_target,
	.get		= at32_get_speed,
	.flags		= CPUFREQ_STICKY,
};

static int __init at32_cpufreq_init(void)
{
	return cpufreq_register_driver(&at32_driver);
}

arch_initcall(at32_cpufreq_init);
