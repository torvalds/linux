/*
 * SMP support for iSeries machines.
 *
 * Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 *
 * Plus various changes from other IBM teams...
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/iseries/hv_call.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/system.h>

static void smp_iSeries_cause_ipi(int cpu, unsigned long data)
{
	HvCall_sendIPI(&(paca[cpu]));
}

static int smp_iSeries_probe(void)
{
	return cpumask_weight(cpu_possible_mask);
}

static int smp_iSeries_kick_cpu(int nr)
{
	BUG_ON((nr < 0) || (nr >= NR_CPUS));

	/* Verify that our partition has a processor nr */
	if (lppaca_of(nr).dyn_proc_status >= 2)
		return -ENOENT;

	/* The processor is currently spinning, waiting
	 * for the cpu_start field to become non-zero
	 * After we set cpu_start, the processor will
	 * continue on to secondary_start in iSeries_head.S
	 */
	paca[nr].cpu_start = 1;

	return 0;
}

static void __devinit smp_iSeries_setup_cpu(int nr)
{
}

static struct smp_ops_t iSeries_smp_ops = {
	.message_pass = smp_muxed_ipi_message_pass,
	.cause_ipi    = smp_iSeries_cause_ipi,
	.probe        = smp_iSeries_probe,
	.kick_cpu     = smp_iSeries_kick_cpu,
	.setup_cpu    = smp_iSeries_setup_cpu,
};

/* This is called very early. */
void __init smp_init_iSeries(void)
{
	smp_ops = &iSeries_smp_ops;
}
