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

struct cpufreq_target {
	struct cpufreq_policy	*policy;
	unsigned int		freq;
};

static unsigned int sh_cpufreq_get(unsigned int cpu)
{
	return (clk_get_rate(&per_cpu(sh_cpuclk, cpu)) + 500) / 1000;
}

static long __sh_cpufreq_target(void *arg)
{
	struct cpufreq_target *target = arg;
	struct cpufreq_policy *policy = target->policy;
	int cpu = policy->cpu;
	struct clk *cpuclk = &per_cpu(sh_cpuclk, cpu);
	struct cpufreq_freqs freqs;
	struct device *dev;
	long freq;

	if (smp_processor_id() != cpu)
		return -ENODEV;

	dev = get_cpu_device(cpu);

	/* Convert target_freq from kHz to Hz */
	freq = clk_round_rate(cpuclk, target->freq * 1000);

	if (freq < (policy->min * 1000) || freq > (policy->max * 1000))
		return -EINVAL;

	dev_dbg(dev, "requested frequency %u Hz\n", target->freq * 1000);

	freqs.old	= sh_cpufreq_get(cpu);
	freqs.new	= (freq + 500) / 1000;
	freqs.flags	= 0;

	cpufreq_freq_transition_begin(target->policy, &freqs);
	clk_set_rate(cpuclk, freq);
	cpufreq_freq_transition_end(target->policy, &freqs, 0);

	dev_dbg(dev, "set frequency %lu Hz\n", freq);
	return 0;
}

/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int sh_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	struct cpufreq_target data = { .policy = policy, .freq = target_freq };

	return work_on_cpu(policy->cpu, __sh_cpufreq_target, &data);
}

static int sh_cpufreq_verify(struct cpufreq_policy *policy)
{
	struct clk *cpuclk = &per_cpu(sh_cpuclk, policy->cpu);
	struct cpufreq_frequency_table *freq_table;

	freq_table = cpuclk->nr_freqs ? cpuclk->freq_table : NULL;
	if (freq_table)
		return cpufreq_frequency_table_verify(policy, freq_table);

	cpufreq_verify_within_cpu_limits(policy);

	policy->min = (clk_round_rate(cpuclk, 1) + 500) / 1000;
	policy->max = (clk_round_rate(cpuclk, ~0UL) + 500) / 1000;

	cpufreq_verify_within_cpu_limits(policy);
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

	freq_table = cpuclk->nr_freqs ? cpuclk->freq_table : NULL;
	if (freq_table) {
		int result;

		result = cpufreq_table_validate_and_show(policy, freq_table);
		if (result)
			return result;
	} else {
		dev_notice(dev, "no frequency table found, falling back "
			   "to rate rounding.\n");

		policy->min = policy->cpuinfo.min_freq =
			(clk_round_rate(cpuclk, 1) + 500) / 1000;
		policy->max = policy->cpuinfo.max_freq =
			(clk_round_rate(cpuclk, ~0UL) + 500) / 1000;
	}

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

	clk_put(cpuclk);

	return 0;
}

static struct cpufreq_driver sh_cpufreq_driver = {
	.name		= "sh",
	.flags		= CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING,
	.get		= sh_cpufreq_get,
	.target		= sh_cpufreq_target,
	.verify		= sh_cpufreq_verify,
	.init		= sh_cpufreq_cpu_init,
	.exit		= sh_cpufreq_cpu_exit,
	.attr		= cpufreq_generic_attr,
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
