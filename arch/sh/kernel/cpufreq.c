/*
 * arch/sh/kernel/cpufreq.c
 *
 * cpufreq driver for the SuperH processors.
 *
 * Copyright (C) 2002 - 2007 Paul Mundt
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
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/sched.h>	/* set_cpus_allowed() */
#include <linux/clk.h>

static struct clk *cpuclk;

static unsigned int sh_cpufreq_get(unsigned int cpu)
{
	return (clk_get_rate(cpuclk) + 500) / 1000;
}

/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int sh_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int cpu = policy->cpu;
	cpumask_t cpus_allowed;
	struct cpufreq_freqs freqs;
	long freq;

	if (!cpu_online(cpu))
		return -ENODEV;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));

	BUG_ON(smp_processor_id() != cpu);

	/* Convert target_freq from kHz to Hz */
	freq = clk_round_rate(cpuclk, target_freq * 1000);

	if (freq < (policy->min * 1000) || freq > (policy->max * 1000))
		return -EINVAL;

	pr_debug("cpufreq: requested frequency %u Hz\n", target_freq * 1000);

	freqs.cpu	= cpu;
	freqs.old	= sh_cpufreq_get(cpu);
	freqs.new	= (freq + 500) / 1000;
	freqs.flags	= 0;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	set_cpus_allowed(current, cpus_allowed);
	clk_set_rate(cpuclk, freq);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	pr_debug("cpufreq: set frequency %lu Hz\n", freq);

	return 0;
}

static int sh_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	if (!cpu_online(policy->cpu))
		return -ENODEV;

	cpuclk = clk_get(NULL, "cpu_clk");
	if (IS_ERR(cpuclk)) {
		printk(KERN_ERR "cpufreq: couldn't get CPU clk\n");
		return PTR_ERR(cpuclk);
	}

	/* cpuinfo and default policy values */
	policy->cpuinfo.min_freq = (clk_round_rate(cpuclk, 1) + 500) / 1000;
	policy->cpuinfo.max_freq = (clk_round_rate(cpuclk, ~0UL) + 500) / 1000;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;

	policy->cur		= sh_cpufreq_get(policy->cpu);
	policy->min		= policy->cpuinfo.min_freq;
	policy->max		= policy->cpuinfo.max_freq;


	/*
	 * Catch the cases where the clock framework hasn't been wired up
	 * properly to support scaling.
	 */
	if (unlikely(policy->min == policy->max)) {
		printk(KERN_ERR "cpufreq: clock framework rate rounding "
		       "not supported on this CPU.\n");

		clk_put(cpuclk);
		return -EINVAL;
	}

	printk(KERN_INFO "cpufreq: Frequencies - Minimum %u.%03u MHz, "
	       "Maximum %u.%03u MHz.\n",
	       policy->min / 1000, policy->min % 1000,
	       policy->max / 1000, policy->max % 1000);

	return 0;
}

static int sh_cpufreq_verify(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     policy->cpuinfo.max_freq);
	return 0;
}

static int sh_cpufreq_exit(struct cpufreq_policy *policy)
{
	clk_put(cpuclk);
	return 0;
}

static struct cpufreq_driver sh_cpufreq_driver = {
	.owner		= THIS_MODULE,
	.name		= "sh",
	.init		= sh_cpufreq_cpu_init,
	.verify		= sh_cpufreq_verify,
	.target		= sh_cpufreq_target,
	.get		= sh_cpufreq_get,
	.exit		= sh_cpufreq_exit,
};

static int __init sh_cpufreq_module_init(void)
{
	printk(KERN_INFO "cpufreq: SuperH CPU frequency driver.\n");
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
