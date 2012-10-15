/*
 * SMP support for PowerNV machines.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>

#include <asm/irq.h>
#include <asm/smp.h>
#include <asm/paca.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/firmware.h>
#include <asm/rtas.h>
#include <asm/vdso_datapage.h>
#include <asm/cputhreads.h>
#include <asm/xics.h>
#include <asm/opal.h>

#include "powernv.h"

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static void __cpuinit pnv_smp_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();
}

static int pnv_smp_cpu_bootable(unsigned int nr)
{
	/* Special case - we inhibit secondary thread startup
	 * during boot if the user requests it.
	 */
	if (system_state < SYSTEM_RUNNING && cpu_has_feature(CPU_FTR_SMT)) {
		if (!smt_enabled_at_boot && cpu_thread_in_core(nr) != 0)
			return 0;
		if (smt_enabled_at_boot
		    && cpu_thread_in_core(nr) >= smt_enabled_at_boot)
			return 0;
	}

	return 1;
}

int __devinit pnv_smp_kick_cpu(int nr)
{
	unsigned int pcpu = get_hard_smp_processor_id(nr);
	unsigned long start_here = __pa(*((unsigned long *)
					  generic_secondary_smp_init));
	long rc;

	BUG_ON(nr < 0 || nr >= NR_CPUS);

	/* On OPAL v2 the CPU are still spinning inside OPAL itself,
	 * get them back now
	 */
	if (!paca[nr].cpu_start && firmware_has_feature(FW_FEATURE_OPALv2)) {
		pr_devel("OPAL: Starting CPU %d (HW 0x%x)...\n", nr, pcpu);
		rc = opal_start_cpu(pcpu, start_here);
		if (rc != OPAL_SUCCESS)
			pr_warn("OPAL Error %ld starting CPU %d\n",
				rc, nr);
	}
	return smp_generic_kick_cpu(nr);
}

#ifdef CONFIG_HOTPLUG_CPU

static int pnv_smp_cpu_disable(void)
{
	int cpu = smp_processor_id();

	/* This is identical to pSeries... might consolidate by
	 * moving migrate_irqs_away to a ppc_md with default to
	 * the generic fixup_irqs. --BenH.
	 */
	set_cpu_online(cpu, false);
	vdso_data->processorCount--;
	if (cpu == boot_cpuid)
		boot_cpuid = cpumask_any(cpu_online_mask);
	xics_migrate_irqs_away();
	return 0;
}

static void pnv_smp_cpu_kill_self(void)
{
	unsigned int cpu;

	/* Standard hot unplug procedure */
	local_irq_disable();
	idle_task_exit();
	current->active_mm = NULL; /* for sanity */
	cpu = smp_processor_id();
	DBG("CPU%d offline\n", cpu);
	generic_set_cpu_dead(cpu);
	smp_wmb();

	/* We don't want to take decrementer interrupts while we are offline,
	 * so clear LPCR:PECE1. We keep PECE2 enabled.
	 */
	mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) & ~(u64)LPCR_PECE1);
	while (!generic_check_cpu_restart(cpu)) {
		power7_nap();
		if (!generic_check_cpu_restart(cpu)) {
			DBG("CPU%d Unexpected exit while offline !\n", cpu);
			/* We may be getting an IPI, so we re-enable
			 * interrupts to process it, it will be ignored
			 * since we aren't online (hopefully)
			 */
			local_irq_enable();
			local_irq_disable();
		}
	}
	mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) | LPCR_PECE1);
	DBG("CPU%d coming online...\n", cpu);
}

#endif /* CONFIG_HOTPLUG_CPU */

static struct smp_ops_t pnv_smp_ops = {
	.message_pass	= smp_muxed_ipi_message_pass,
	.cause_ipi	= NULL,	/* Filled at runtime by xics_smp_probe() */
	.probe		= xics_smp_probe,
	.kick_cpu	= pnv_smp_kick_cpu,
	.setup_cpu	= pnv_smp_setup_cpu,
	.cpu_bootable	= pnv_smp_cpu_bootable,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= pnv_smp_cpu_disable,
	.cpu_die	= generic_cpu_die,
#endif /* CONFIG_HOTPLUG_CPU */
};

/* This is called very early during platform setup_arch */
void __init pnv_smp_init(void)
{
	smp_ops = &pnv_smp_ops;

	/* XXX We don't yet have a proper entry point from HAL, for
	 * now we rely on kexec-style entry from BML
	 */

#ifdef CONFIG_PPC_RTAS
	/* Non-lpar has additional take/give timebase */
	if (rtas_token("freeze-time-base") != RTAS_UNKNOWN_SERVICE) {
		smp_ops->give_timebase = rtas_give_timebase;
		smp_ops->take_timebase = rtas_take_timebase;
	}
#endif /* CONFIG_PPC_RTAS */

#ifdef CONFIG_HOTPLUG_CPU
	ppc_md.cpu_die	= pnv_smp_cpu_kill_self;
#endif
}
