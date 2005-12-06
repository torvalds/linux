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

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
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

static unsigned long iSeries_smp_message[NR_CPUS];

void iSeries_smp_message_recv(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	int msg;

	if (num_online_cpus() < 2)
		return;

	for (msg = 0; msg < 4; msg++)
		if (test_and_clear_bit(msg, &iSeries_smp_message[cpu]))
			smp_message_recv(msg, regs);
}

static inline void smp_iSeries_do_message(int cpu, int msg)
{
	set_bit(msg, &iSeries_smp_message[cpu]);
	HvCall_sendIPI(&(paca[cpu]));
}

static void smp_iSeries_message_pass(int target, int msg)
{
	int i;

	if (target < NR_CPUS)
		smp_iSeries_do_message(target, msg);
	else {
		for_each_online_cpu(i) {
			if ((target == MSG_ALL_BUT_SELF) &&
					(i == smp_processor_id()))
				continue;
			smp_iSeries_do_message(i, msg);
		}
	}
}

static int smp_iSeries_probe(void)
{
	return cpus_weight(cpu_possible_map);
}

static void smp_iSeries_kick_cpu(int nr)
{
	BUG_ON((nr < 0) || (nr >= NR_CPUS));

	/* Verify that our partition has a processor nr */
	if (paca[nr].lppaca.dyn_proc_status >= 2)
		return;

	/* The processor is currently spinning, waiting
	 * for the cpu_start field to become non-zero
	 * After we set cpu_start, the processor will
	 * continue on to secondary_start in iSeries_head.S
	 */
	paca[nr].cpu_start = 1;
}

static void __devinit smp_iSeries_setup_cpu(int nr)
{
}

static struct smp_ops_t iSeries_smp_ops = {
	.message_pass = smp_iSeries_message_pass,
	.probe        = smp_iSeries_probe,
	.kick_cpu     = smp_iSeries_kick_cpu,
	.setup_cpu    = smp_iSeries_setup_cpu,
};

/* This is called very early. */
void __init smp_init_iSeries(void)
{
	smp_ops = &iSeries_smp_ops;
}
