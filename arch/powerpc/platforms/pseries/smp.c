/*
 * SMP support for pSeries machines.
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
#include <asm/time.h>
#include <asm/machdep.h>
#include "xics.h"
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/system.h>
#include <asm/rtas.h>
#include <asm/pSeries_reconfig.h>
#include <asm/mpic.h>
#include <asm/vdso_datapage.h>

#include "plpar_wrappers.h"

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/*
 * The primary thread of each non-boot processor is recorded here before
 * smp init.
 */
static cpumask_t of_spin_map;

extern void pSeries_secondary_smp_init(unsigned long);

#ifdef CONFIG_HOTPLUG_CPU

/* Get state of physical CPU.
 * Return codes:
 *	0	- The processor is in the RTAS stopped state
 *	1	- stop-self is in progress
 *	2	- The processor is not in the RTAS stopped state
 *	-1	- Hardware Error
 *	-2	- Hardware Busy, Try again later.
 */
static int query_cpu_stopped(unsigned int pcpu)
{
	int cpu_status;
	int status, qcss_tok;

	qcss_tok = rtas_token("query-cpu-stopped-state");
	if (qcss_tok == RTAS_UNKNOWN_SERVICE)
		return -1;
	status = rtas_call(qcss_tok, 1, 2, &cpu_status, pcpu);
	if (status != 0) {
		printk(KERN_ERR
		       "RTAS query-cpu-stopped-state failed: %i\n", status);
		return status;
	}

	return cpu_status;
}

static int pSeries_cpu_disable(void)
{
	int cpu = smp_processor_id();

	cpu_clear(cpu, cpu_online_map);
	vdso_data->processorCount--;

	/*fix boot_cpuid here*/
	if (cpu == boot_cpuid)
		boot_cpuid = any_online_cpu(cpu_online_map);

	/* FIXME: abstract this to not be platform specific later on */
	xics_migrate_irqs_away();
	return 0;
}

static void pSeries_cpu_die(unsigned int cpu)
{
	int tries;
	int cpu_status;
	unsigned int pcpu = get_hard_smp_processor_id(cpu);

	for (tries = 0; tries < 25; tries++) {
		cpu_status = query_cpu_stopped(pcpu);
		if (cpu_status == 0 || cpu_status == -1)
			break;
		msleep(200);
	}
	if (cpu_status != 0) {
		printk("Querying DEAD? cpu %i (%i) shows %i\n",
		       cpu, pcpu, cpu_status);
	}

	/* Isolation and deallocation are definatly done by
	 * drslot_chrp_cpu.  If they were not they would be
	 * done here.  Change isolate state to Isolate and
	 * change allocation-state to Unusable.
	 */
	paca[cpu].cpu_start = 0;
}

/*
 * Update cpu_present_map and paca(s) for a new cpu node.  The wrinkle
 * here is that a cpu device node may represent up to two logical cpus
 * in the SMT case.  We must honor the assumption in other code that
 * the logical ids for sibling SMT threads x and y are adjacent, such
 * that x^1 == y and y^1 == x.
 */
static int pSeries_add_processor(struct device_node *np)
{
	unsigned int cpu;
	cpumask_t candidate_map, tmp = CPU_MASK_NONE;
	int err = -ENOSPC, len, nthreads, i;
	u32 *intserv;

	intserv = (u32 *)get_property(np, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return 0;

	nthreads = len / sizeof(u32);
	for (i = 0; i < nthreads; i++)
		cpu_set(i, tmp);

	lock_cpu_hotplug();

	BUG_ON(!cpus_subset(cpu_present_map, cpu_possible_map));

	/* Get a bitmap of unoccupied slots. */
	cpus_xor(candidate_map, cpu_possible_map, cpu_present_map);
	if (cpus_empty(candidate_map)) {
		/* If we get here, it most likely means that NR_CPUS is
		 * less than the partition's max processors setting.
		 */
		printk(KERN_ERR "Cannot add cpu %s; this system configuration"
		       " supports %d logical cpus.\n", np->full_name,
		       cpus_weight(cpu_possible_map));
		goto out_unlock;
	}

	while (!cpus_empty(tmp))
		if (cpus_subset(tmp, candidate_map))
			/* Found a range where we can insert the new cpu(s) */
			break;
		else
			cpus_shift_left(tmp, tmp, nthreads);

	if (cpus_empty(tmp)) {
		printk(KERN_ERR "Unable to find space in cpu_present_map for"
		       " processor %s with %d thread(s)\n", np->name,
		       nthreads);
		goto out_unlock;
	}

	for_each_cpu_mask(cpu, tmp) {
		BUG_ON(cpu_isset(cpu, cpu_present_map));
		cpu_set(cpu, cpu_present_map);
		set_hard_smp_processor_id(cpu, *intserv++);
	}
	err = 0;
out_unlock:
	unlock_cpu_hotplug();
	return err;
}

/*
 * Update the present map for a cpu node which is going away, and set
 * the hard id in the paca(s) to -1 to be consistent with boot time
 * convention for non-present cpus.
 */
static void pSeries_remove_processor(struct device_node *np)
{
	unsigned int cpu;
	int len, nthreads, i;
	u32 *intserv;

	intserv = (u32 *)get_property(np, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return;

	nthreads = len / sizeof(u32);

	lock_cpu_hotplug();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;
			BUG_ON(cpu_online(cpu));
			cpu_clear(cpu, cpu_present_map);
			set_hard_smp_processor_id(cpu, -1);
			break;
		}
		if (cpu == NR_CPUS)
			printk(KERN_WARNING "Could not find cpu to remove "
			       "with physical id 0x%x\n", intserv[i]);
	}
	unlock_cpu_hotplug();
}

static int pSeries_smp_notifier(struct notifier_block *nb, unsigned long action, void *node)
{
	int err = NOTIFY_OK;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		if (pSeries_add_processor(node))
			err = NOTIFY_BAD;
		break;
	case PSERIES_RECONFIG_REMOVE:
		pSeries_remove_processor(node);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block pSeries_smp_nb = {
	.notifier_call = pSeries_smp_notifier,
};

#endif /* CONFIG_HOTPLUG_CPU */

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
					       pSeries_secondary_smp_init));
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

	status = rtas_call(start_cpu, 3, 1, NULL, pcpu, start_here, pcpu);
	if (status != 0) {
		printk(KERN_ERR "start-cpu failed: %i\n", status);
		return 0;
	}

	return 1;
}

#ifdef CONFIG_XICS
static inline void smp_xics_do_message(int cpu, int msg)
{
	set_bit(msg, &xics_ipi_message[cpu].value);
	mb();
	xics_cause_IPI(cpu);
}

static void smp_xics_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		smp_xics_do_message(target, msg);
	} else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			smp_xics_do_message(i, msg);
		}
	}
}

static int __init smp_xics_probe(void)
{
	xics_request_IPIs();

	return cpus_weight(cpu_possible_map);
}

static void __devinit smp_xics_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();

	if (firmware_has_feature(FW_FEATURE_SPLPAR))
		vpa_init(cpu);

	cpu_clear(cpu, of_spin_map);

}
#endif /* CONFIG_XICS */

static DEFINE_SPINLOCK(timebase_lock);
static unsigned long timebase = 0;

static void __devinit pSeries_give_timebase(void)
{
	spin_lock(&timebase_lock);
	rtas_call(rtas_token("freeze-time-base"), 0, 1, NULL);
	timebase = get_tb();
	spin_unlock(&timebase_lock);

	while (timebase)
		barrier();
	rtas_call(rtas_token("thaw-time-base"), 0, 1, NULL);
}

static void __devinit pSeries_take_timebase(void)
{
	while (!timebase)
		barrier();
	spin_lock(&timebase_lock);
	set_tb(timebase >> 32, timebase & 0xffffffff);
	timebase = 0;
	spin_unlock(&timebase_lock);
}

static void __devinit smp_pSeries_kick_cpu(int nr)
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

static int smp_pSeries_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.  Odd-numbered
	 * cpus are assumed to be secondary threads.
	 */
	if (system_state < SYSTEM_RUNNING &&
	    cpu_has_feature(CPU_FTR_SMT) &&
	    !smt_enabled_at_boot && nr % 2 != 0)
		return 0;

	return 1;
}
#ifdef CONFIG_MPIC
static struct smp_ops_t pSeries_mpic_smp_ops = {
	.message_pass	= smp_mpic_message_pass,
	.probe		= smp_mpic_probe,
	.kick_cpu	= smp_pSeries_kick_cpu,
	.setup_cpu	= smp_mpic_setup_cpu,
};
#endif
#ifdef CONFIG_XICS
static struct smp_ops_t pSeries_xics_smp_ops = {
	.message_pass	= smp_xics_message_pass,
	.probe		= smp_xics_probe,
	.kick_cpu	= smp_pSeries_kick_cpu,
	.setup_cpu	= smp_xics_setup_cpu,
	.cpu_bootable	= smp_pSeries_cpu_bootable,
};
#endif

/* This is called very early */
void __init smp_init_pSeries(void)
{
	int i;

	DBG(" -> smp_init_pSeries()\n");

	switch (ppc64_interrupt_controller) {
#ifdef CONFIG_MPIC
	case IC_OPEN_PIC:
		smp_ops = &pSeries_mpic_smp_ops;
		break;
#endif
#ifdef CONFIG_XICS
	case IC_PPC_XIC:
		smp_ops = &pSeries_xics_smp_ops;
		break;
#endif
	default:
		panic("Invalid interrupt controller");
	}

#ifdef CONFIG_HOTPLUG_CPU
	smp_ops->cpu_disable = pSeries_cpu_disable;
	smp_ops->cpu_die = pSeries_cpu_die;

	/* Processors can be added/removed only on LPAR */
	if (platform_is_lpar())
		pSeries_reconfig_notifier_register(&pSeries_smp_nb);
#endif

	/* Mark threads which are still spinning in hold loops. */
	if (cpu_has_feature(CPU_FTR_SMT)) {
		for_each_present_cpu(i) { 
			if (i % 2 == 0)
				/*
				 * Even-numbered logical cpus correspond to
				 * primary threads.
				 */
				cpu_set(i, of_spin_map);
		}
	} else {
		of_spin_map = cpu_present_map;
	}

	cpu_clear(boot_cpuid, of_spin_map);

	/* Non-lpar has additional take/give timebase */
	if (rtas_token("freeze-time-base") != RTAS_UNKNOWN_SERVICE) {
		smp_ops->give_timebase = pSeries_give_timebase;
		smp_ops->take_timebase = pSeries_take_timebase;
	}

	DBG(" <- smp_init_pSeries()\n");
}

