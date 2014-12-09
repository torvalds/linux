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
#include <asm/runlatch.h>
#include <asm/code-patching.h>
#include <asm/dbell.h>

#include "powernv.h"

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

static void pnv_smp_setup_cpu(int cpu)
{
	if (cpu != boot_cpuid)
		xics_setup_cpu();

#ifdef CONFIG_PPC_DOORBELL
	if (cpu_has_feature(CPU_FTR_DBELL))
		doorbell_setup_this_cpu();
#endif
}

static int pnv_smp_kick_cpu(int nr)
{
	unsigned int pcpu = get_hard_smp_processor_id(nr);
	unsigned long start_here =
			__pa(ppc_function_entry(generic_secondary_smp_init));
	long rc;

	BUG_ON(nr < 0 || nr >= NR_CPUS);

	/*
	 * If we already started or OPALv2 is not supported, we just
	 * kick the CPU via the PACA
	 */
	if (paca[nr].cpu_start || !firmware_has_feature(FW_FEATURE_OPALv2))
		goto kick;

	/*
	 * At this point, the CPU can either be spinning on the way in
	 * from kexec or be inside OPAL waiting to be started for the
	 * first time. OPAL v3 allows us to query OPAL to know if it
	 * has the CPUs, so we do that
	 */
	if (firmware_has_feature(FW_FEATURE_OPALv3)) {
		uint8_t status;

		rc = opal_query_cpu_status(pcpu, &status);
		if (rc != OPAL_SUCCESS) {
			pr_warn("OPAL Error %ld querying CPU %d state\n",
				rc, nr);
			return -ENODEV;
		}

		/*
		 * Already started, just kick it, probably coming from
		 * kexec and spinning
		 */
		if (status == OPAL_THREAD_STARTED)
			goto kick;

		/*
		 * Available/inactive, let's kick it
		 */
		if (status == OPAL_THREAD_INACTIVE) {
			pr_devel("OPAL: Starting CPU %d (HW 0x%x)...\n",
				 nr, pcpu);
			rc = opal_start_cpu(pcpu, start_here);
			if (rc != OPAL_SUCCESS) {
				pr_warn("OPAL Error %ld starting CPU %d\n",
					rc, nr);
				return -ENODEV;
			}
		} else {
			/*
			 * An unavailable CPU (or any other unknown status)
			 * shouldn't be started. It should also
			 * not be in the possible map but currently it can
			 * happen
			 */
			pr_devel("OPAL: CPU %d (HW 0x%x) is unavailable"
				 " (status %d)...\n", nr, pcpu, status);
			return -ENODEV;
		}
	} else {
		/*
		 * On OPAL v2, we just kick it and hope for the best,
		 * we must not test the error from opal_start_cpu() or
		 * we would fail to get CPUs from kexec.
		 */
		opal_start_cpu(pcpu, start_here);
	}
 kick:
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
	unsigned long srr1;
	u32 idle_states;

	/* Standard hot unplug procedure */
	local_irq_disable();
	idle_task_exit();
	current->active_mm = NULL; /* for sanity */
	cpu = smp_processor_id();
	DBG("CPU%d offline\n", cpu);
	generic_set_cpu_dead(cpu);
	smp_wmb();

	idle_states = pnv_get_supported_cpuidle_states();
	/* We don't want to take decrementer interrupts while we are offline,
	 * so clear LPCR:PECE1. We keep PECE2 enabled.
	 */
	mtspr(SPRN_LPCR, mfspr(SPRN_LPCR) & ~(u64)LPCR_PECE1);
	while (!generic_check_cpu_restart(cpu)) {
		ppc64_runlatch_off();
		if ((idle_states & OPAL_PM_SLEEP_ENABLED) ||
				(idle_states & OPAL_PM_SLEEP_ENABLED_ER1))
			srr1 = power7_sleep();
		else
			srr1 = power7_nap(1);
		ppc64_runlatch_on();

		/*
		 * If the SRR1 value indicates that we woke up due to
		 * an external interrupt, then clear the interrupt.
		 * We clear the interrupt before checking for the
		 * reason, so as to avoid a race where we wake up for
		 * some other reason, find nothing and clear the interrupt
		 * just as some other cpu is sending us an interrupt.
		 * If we returned from power7_nap as a result of
		 * having finished executing in a KVM guest, then srr1
		 * contains 0.
		 */
		if ((srr1 & SRR1_WAKEMASK) == SRR1_WAKEEE) {
			icp_native_flush_interrupt();
			local_paca->irq_happened &= PACA_IRQ_HARD_DIS;
			smp_mb();
		}

		if (cpu_core_split_required())
			continue;

		if (!generic_check_cpu_restart(cpu))
			DBG("CPU%d Unexpected exit while offline !\n", cpu);
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
	.cpu_bootable	= smp_generic_cpu_bootable,
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
