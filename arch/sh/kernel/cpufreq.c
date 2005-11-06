/*
 * arch/sh/kernel/cpufreq.c
 *
 * cpufreq driver for the SuperH processors.
 *
 * Copyright (C) 2002, 2003, 2004, 2005 Paul Mundt
 * Copyright (C) 2002 M. R. Brown
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/types.h>
#include <linux/cpufreq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/sched.h>	/* set_cpus_allowed() */

#include <asm/processor.h>
#include <asm/watchdog.h>
#include <asm/freq.h>
#include <asm/io.h>

/*
 * For SuperH, each policy change requires that we change the IFC, BFC, and
 * PFC at the same time.  Here we define sane values that won't trash the
 * system.
 *
 * Note the max set is computed at runtime, we use the divisors that we booted
 * with to setup our maximum operating frequencies.
 */
struct clock_set {
	unsigned int ifc;
	unsigned int bfc;
	unsigned int pfc;
} clock_sets[] = {
#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH2)
	{ 0, 0, 0 },	/* not implemented yet */
#elif defined(CONFIG_CPU_SH4)
	{ 4, 8, 8 },	/* min - IFC: 1/4, BFC: 1/8, PFC: 1/8 */
	{ 1, 2, 2 },	/* max - IFC: 1, BFC: 1/2, PFC: 1/2 */
#endif
};

#define MIN_CLOCK_SET	0
#define MAX_CLOCK_SET	(ARRAY_SIZE(clock_sets) - 1)

/*
 * For the time being, we only support two frequencies, which in turn are
 * aimed at the POWERSAVE and PERFORMANCE policies, which in turn are derived
 * directly from the respective min/max clock sets. Technically we could
 * support a wider range of frequencies, but these vary far too much for each
 * CPU subtype (and we'd have to construct a frequency table for each subtype).
 *
 * Maybe something to implement in the future..
 */
#define SH_FREQ_MAX	0
#define SH_FREQ_MIN	1

static struct cpufreq_frequency_table sh_freqs[] = {
	{ SH_FREQ_MAX,	0 },
	{ SH_FREQ_MIN,	0 },
	{ 0,		CPUFREQ_TABLE_END },
};

static void sh_cpufreq_update_clocks(unsigned int set)
{
	current_cpu_data.cpu_clock = current_cpu_data.master_clock / clock_sets[set].ifc;
	current_cpu_data.bus_clock = current_cpu_data.master_clock / clock_sets[set].bfc;
	current_cpu_data.module_clock = current_cpu_data.master_clock / clock_sets[set].pfc;
	current_cpu_data.loops_per_jiffy = loops_per_jiffy;
}

/* XXX: This needs to be split out per CPU and CPU subtype. */
/*
 * Here we notify other drivers of the proposed change and the final change.
 */
static int sh_cpufreq_setstate(unsigned int cpu, unsigned int set)
{
	unsigned short frqcr = ctrl_inw(FRQCR);
	cpumask_t cpus_allowed;
	struct cpufreq_freqs freqs;

	if (!cpu_online(cpu))
		return -ENODEV;

	cpus_allowed = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(cpu));

	BUG_ON(smp_processor_id() != cpu);

	freqs.cpu = cpu;
	freqs.old = current_cpu_data.cpu_clock / 1000;
	freqs.new = (current_cpu_data.master_clock / clock_sets[set].ifc) / 1000;

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
#if defined(CONFIG_CPU_SH3)
	frqcr |= (newstate & 0x4000) << 14;
	frqcr |= (newstate & 0x000c) <<  2;
#elif defined(CONFIG_CPU_SH4)
	/*
	 * FRQCR.PLL2EN is 1, we need to allow the PLL to stabilize by
	 * initializing the WDT.
	 */
	if (frqcr & (1 << 9)) {
		__u8 csr;

		/*
		 * Set the overflow period to the highest available,
		 * in this case a 1/4096 division ratio yields a 5.25ms
		 * overflow period. See asm-sh/watchdog.h for more
		 * information and a range of other divisors.
		 */
		csr = sh_wdt_read_csr();
		csr |= WTCSR_CKS_4096;
		sh_wdt_write_csr(csr);

		sh_wdt_write_cnt(0);
	}
	frqcr &= 0x0e00;	/* Clear ifc, bfc, pfc */
	frqcr |= get_ifc_value(clock_sets[set].ifc) << 6;
	frqcr |= get_bfc_value(clock_sets[set].bfc) << 3;
	frqcr |= get_pfc_value(clock_sets[set].pfc);
#endif
	ctrl_outw(frqcr, FRQCR);
	sh_cpufreq_update_clocks(set);

	set_cpus_allowed(current, cpus_allowed);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static int sh_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int min_freq, max_freq;
	unsigned int ifc, bfc, pfc;

	if (!cpu_online(policy->cpu))
		return -ENODEV;

	/* Update our maximum clock set */
	get_current_frequency_divisors(&ifc, &bfc, &pfc);
	clock_sets[MAX_CLOCK_SET].ifc = ifc;
	clock_sets[MAX_CLOCK_SET].bfc = bfc;
	clock_sets[MAX_CLOCK_SET].pfc = pfc;

	/* Convert from Hz to kHz */
	max_freq = current_cpu_data.cpu_clock / 1000;
	min_freq = (current_cpu_data.master_clock / clock_sets[MIN_CLOCK_SET].ifc) / 1000;
	
	sh_freqs[SH_FREQ_MAX].frequency = max_freq;
	sh_freqs[SH_FREQ_MIN].frequency = min_freq;

	/* cpuinfo and default policy values */
	policy->governor                   = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	policy->cur                        = max_freq;

	return cpufreq_frequency_table_cpuinfo(policy, &sh_freqs[0]);
}

static int sh_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &sh_freqs[0]);
}

static int sh_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int set, idx = 0;

	if (cpufreq_frequency_table_target(policy, &sh_freqs[0], target_freq, relation, &idx))
		return -EINVAL;

	set = (idx == SH_FREQ_MIN) ? MIN_CLOCK_SET : MAX_CLOCK_SET;

	sh_cpufreq_setstate(policy->cpu, set);

	return 0;
}

static struct cpufreq_driver sh_cpufreq_driver = {
	.owner		= THIS_MODULE,
	.name		= "SH cpufreq",
	.init		= sh_cpufreq_cpu_init,
	.verify		= sh_cpufreq_verify,
	.target		= sh_cpufreq_target,
};

static int __init sh_cpufreq_init(void)
{
	if (!current_cpu_data.cpu_clock)
		return -EINVAL;
	if (cpufreq_register_driver(&sh_cpufreq_driver))
		return -EINVAL;

	return 0;
}

static void __exit sh_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&sh_cpufreq_driver);
}

module_init(sh_cpufreq_init);
module_exit(sh_cpufreq_exit);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("cpufreq driver for SuperH");
MODULE_LICENSE("GPL");

