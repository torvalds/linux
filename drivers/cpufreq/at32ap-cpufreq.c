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
#include <linux/export.h>
#include <linux/slab.h>

static struct cpufreq_frequency_table *freq_table;

static unsigned int	ref_freq;
static unsigned long	loops_per_jiffy_ref;

static int at32_set_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned int old_freq, new_freq;

	old_freq = policy->cur;
	new_freq = freq_table[index].frequency;

	if (!ref_freq) {
		ref_freq = old_freq;
		loops_per_jiffy_ref = boot_cpu_data.loops_per_jiffy;
	}

	if (old_freq < new_freq)
		boot_cpu_data.loops_per_jiffy = cpufreq_scale(
				loops_per_jiffy_ref, ref_freq, new_freq);
	clk_set_rate(policy->clk, new_freq * 1000);
	if (new_freq < old_freq)
		boot_cpu_data.loops_per_jiffy = cpufreq_scale(
				loops_per_jiffy_ref, ref_freq, new_freq);

	return 0;
}

static int at32_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	unsigned int frequency, rate, min_freq;
	struct clk *cpuclk;
	int retval, steps, i;

	if (policy->cpu != 0)
		return -EINVAL;

	cpuclk = clk_get(NULL, "cpu");
	if (IS_ERR(cpuclk)) {
		pr_debug("cpufreq: could not get CPU clk\n");
		retval = PTR_ERR(cpuclk);
		goto out_err;
	}

	min_freq = (clk_round_rate(cpuclk, 1) + 500) / 1000;
	frequency = (clk_round_rate(cpuclk, ~0UL) + 500) / 1000;
	policy->cpuinfo.transition_latency = 0;

	/*
	 * AVR32 CPU frequency rate scales in power of two between maximum and
	 * minimum, also add space for the table end marker.
	 *
	 * Further validate that the frequency is usable, and append it to the
	 * frequency table.
	 */
	steps = fls(frequency / min_freq) + 1;
	freq_table = kzalloc(steps * sizeof(struct cpufreq_frequency_table),
			GFP_KERNEL);
	if (!freq_table) {
		retval = -ENOMEM;
		goto out_err_put_clk;
	}

	for (i = 0; i < (steps - 1); i++) {
		rate = clk_round_rate(cpuclk, frequency * 1000) / 1000;

		if (rate != frequency)
			freq_table[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			freq_table[i].frequency = frequency;

		frequency /= 2;
	}

	policy->clk = cpuclk;
	freq_table[steps - 1].frequency = CPUFREQ_TABLE_END;

	retval = cpufreq_table_validate_and_show(policy, freq_table);
	if (!retval) {
		printk("cpufreq: AT32AP CPU frequency driver\n");
		return 0;
	}

	kfree(freq_table);
out_err_put_clk:
	clk_put(cpuclk);
out_err:
	return retval;
}

static struct cpufreq_driver at32_driver = {
	.name		= "at32ap",
	.init		= at32_cpufreq_driver_init,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= at32_set_target,
	.get		= cpufreq_generic_get,
	.flags		= CPUFREQ_STICKY,
};

static int __init at32_cpufreq_init(void)
{
	return cpufreq_register_driver(&at32_driver);
}
late_initcall(at32_cpufreq_init);
