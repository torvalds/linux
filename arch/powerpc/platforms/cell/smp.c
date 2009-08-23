/*
 * SMP support for BPA machines.
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
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/system.h>
#include <asm/rtas.h>
#include <asm/cputhreads.h>

#include "interrupt.h"
#include <asm/udbg.h>

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/*
 * The Primary thread of each non-boot processor was started from the OF client
 * interface by prom_hold_cpus and is spinning on secondary_hold_spinloop.
 */
static cpumask_t of_spin_map;

extern void generic_secondary_smp_init(unsigned long);

/**
 * smp_startup_cpu() - start the given cpu
 *
 * At boot time, there is nothing to do for primary threads which were
 * started from Open Firmware.  For anything else, call RTAS with the
 * appropriate start location.
 *
 * Returns:
 *	0	- failure
 *	1	- success
 */
static inline int __devinit smp_startup_cpu(unsigned int lcpu)
{
	int status;
	unsigned long start_here = __pa((u32)*((unsigned long *)
					       generic_secondary_smp_init));
	unsigned int pcpu;
	int start_cpu;

	if (cpu_isset(lcpu, of_spin_map))
		/* Already started by OF and sitting in spin loop */
		return 1;

	pcpu = get_hard_smp_processor_id(lcpu);

	/* Fixup atomic count: it exited inside IRQ handler. */
	task_thread_info(paca[lcpu].__current)->preempt_count	= 0;

	/*
	 * If the RTAS start-cpu token does not exist then presume the
	 * cpu is already spinning.
	 */
	start_cpu = rtas_token("start-cpu");
	if (start_cpu == RTAS_UNKNOWN_SERVICE)
		return 1;

	status = rtas_call(start_cpu, 3, 1, NULL, pcpu, start_here, lcpu);
	if (status != 0) {
		printk(KERN_ERR "start-cpu failed: %i\n", status);
		return 0;
	}

	return 1;
}

static void smp_iic_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		iic_cause_IPI(target, msg);
	} else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			iic_cause_IPI(i, msg);
		}
	}
}

static int __init smp_iic_probe(void)
{
	iic_request_IPIs();

	return cpus_weight(cpu_possible_map);
}

static void __devinit smp_cell_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		iic_setup_cpu();

	/*
	 * change default DABRX to allow user watchpoints
	 */
	mtspr(SPRN_DABRX, DABRX_KERNEL | DABRX_USER);
}

static void __devinit smp_cell_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	if (!smp_startup_cpu(nr))
		return;

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;
}

static int smp_cell_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.  Odd-numbered
	 * cpus are assumed to be secondary threads.
	 */
	if (system_state < SYSTEM_RUNNING &&
	    cpu_has_feature(CPU_FTR_SMT) &&
	    !smt_enabled_at_boot && cpu_thread_in_core(nr) != 0)
		return 0;

	return 1;
}
static struct smp_ops_t bpa_iic_smp_ops = {
	.message_pass	= smp_iic_message_pass,
	.probe		= smp_iic_probe,
	.kick_cpu	= smp_cell_kick_cpu,
	.setup_cpu	= smp_cell_setup_cpu,
	.cpu_bootable	= smp_cell_cpu_bootable,
};

/* This is called very early */
void __init smp_init_cell(void)
{
	int i;

	DBG(" -> smp_init_cell()\n");

	smp_ops = &bpa_iic_smp_ops;

	/* Mark threads which are still spinning in hold loops. */
	if (cpu_has_feature(CPU_FTR_SMT)) {
		for_each_present_cpu(i) {
			if (cpu_thread_in_core(i) == 0)
				cpu_set(i, of_spin_map);
		}
	} else {
		of_spin_map = cpu_present_map;
	}

	cpu_clear(boot_cpuid, of_spin_map);

	/* Non-lpar has additional take/give timebase */
	if (rtas_token("freeze-time-base") != RTAS_UNKNOWN_SERVICE) {
		smp_ops->give_timebase = rtas_give_timebase;
		smp_ops->take_timebase = rtas_take_timebase;
	}

	DBG(" <- smp_init_cell()\n");
}
