/*
 * cpufreq driver for the SuperH processors.
 *
 * Copyright (C) 2002 - 2012 Paul Mundt
 * Copyright (C) 2002 M. R. Brown
 *
 * Clock framework bits from arch/avr32/mach-at32ap/cpufreq.c
 *
 *   Copyright (C) 2004-2007 Atmel Corporation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "cpufreq: " fmt

#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/sched.h>	/* set_cpus_allowed() */
#include <linux/clk.h>
#include <linux/percpu.h>
#include <linux/sh_clk.h>

static DEFINE_PER_CPU(struct clk, sh_cpuclk);

static unsigned int sh_cpufreq_get(unsigned int cpu)
{
	return (clk_get_rate(&per_cpu(sh_cpuclk, cpu)) + 500) / 1000;
}

/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int sh_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int cpu = policy->cpu;
	struct clk *cpuclk = &per_cpu(sh_cpuclk, cpu);
	cpumask_t cpus_allowed;
	struct cpufreq_freqs freqs;
	struct device *dev;
	long freq;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed_ptr(current, cpumask_of(cpu));

	BUG_ON(smp_processor_id() != cpu);

	dev = get_cpu_device(cpu);

	/* Convert target_freq from kHz to Hz */
	freq = clk_round_rate(cpuclk, target_freq * 1000);

	if (freq < (policy->min * 1000) || freq > (policy->max * 1000))
		return -EINVAL;

	dev_dbg(dev, "requested frequency %u Hz\n", target_freq * 1000);

	freqs.old	= sh_cpufreq_get(cpu);
	freqs.new	= (freq + 500) / 1000;
	freqs.flags	= 0;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
	set_cpus_allowed_ptr(current, &cpus_allowed);
	clk_set_rate(cpuclk, freq);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	dev_dbg(dev, "set frequency %lu Hz\n", freq);

	return 0;
}

static int sh_cpufreq_verify(struct cpufreq_policy *policy)
{
	struct clk *cpuclk = &per_cpu(sh_cpuclk, policy->cpu);
	struct cpufreq_frequency_table *freq_table;

	freq_table = cpuclk->nr_freqs ? cpuclk->freq_table : NULL;
	if (freq_table)
		return cpufreq_frequency_table_verify(policy, freq_table);

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	policy->min = (clk_round_rate(cpuclk, 1) + 500) / 1000;
	policy->max = (clk_round_rate(cpuclk, ~0UL) + 500) / 1000;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);

	return 0;
}

static int sh_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct clk *cpuclk = &per_cpu(sh_cpuclk, cpu);
	struct cpufreq_frequency_table *freq_table;
	struct device *dev;

	dev = get_cpu_device(cpu);

	cpuclk = clk_get(dev, "cpu_clk");
	if (IS_ERR(cpuclk)) {
		dev_err(dev, "couldn't get CPU clk\n");
		return PTR_ERR(cpuclk);
	}

	policy->cur = sh_cpufreq_get(cpu);

	freq_table = cpuclk->nr_freqs ? cpuclk->freq_table : NULL;
	if (freq_table) {
		int result;

		result = cpufreq_frequency_table_cpuinfo(policy, freq_table);
		if (!result)
			cpufreq_frequency_table_get_attr(freq_table, cpu);
	} else {
		dev_notice(dev, "no frequency table found, falling back "
			   "to rate rounding.\n");

		policy->min = policy->cpuinfo.min_freq =
			(clk_round_rate(cpuclk, 1) + 500) / 1000;
		policy->max = policy->cpuinfo.max_freq =
			(clk_round_rate(cpuclk, ~0UL) + 500) / 1000;
	}

	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;

	dev_info(dev, "CPU Frequencies - Minimum %u.%03u MHz, "
	       "Maximum %u.%03u MHz.\n",
	       policy->min / 1000, policy->min % 1000,
	       policy->max / 1000, policy->max % 1000);

	return 0;
}

static int sh_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct clk *cpuclk = &per_cpu(sh_cpuclk, cpu);

	cpufreq_frequency_table_put_attr(cpu);
	clk_put(cpuclk);

	return 0;
}

static struct freq_attr *sh_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver sh_cpufreq_driver = {
	.name		= "sh",
	.get		= sh_cpufreq_get,
	.target		= sh_cpufreq_target,
	.verify		= sh_cpufreq_verify,
	.init		= sh_cpufreq_cpu_init,
	.exit		= sh_cpufreq_cpu_exit,
	.attr		= sh_freq_attr,
};

static int __init sh_cpufreq_module_init(void)
{
	pr_notice("SuperH CPU frequency driver.\n");
	return cpufreq_register_driver(&sh_cpufreq_driver);
}

static void __exit sh_cpufreq_module_exit(void)
{
	cpufreq_unregister_driver(&sh_cpufreq_driver);
}

module_init(sh_cpufreq_module_init);
module_exit(sh_cpufreq_module_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("cpufreq driver for SuperH");
MODULE_LICENSE("GPL");
