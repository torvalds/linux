// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SMP support for PowerNV machines.
 *
 * Copyright 2011 IBM Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/hotplug.h>
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
#include <asm/vdso_datapage.h>
#include <asm/cputhreads.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/opal.h>
#include <asm/runlatch.h>
#include <asm/code-patching.h>
#include <asm/dbell.h>
#include <asm/kvm_ppc.h>
#include <asm/ppc-opcode.h>
#include <asm/cpuidle.h>
#include <asm/kexec.h>
#include <asm/reg.h>
#include <asm/powernv.h>

#include "powernv.h"

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...) do { } while (0)
#endif

static void pnv_smp_setup_cpu(int cpu)
{
	/*
	 * P9 workaround for CI vector load (see traps.c),
	 * enable the corresponding HMI interrupt
	 */
	if (pvr_version_is(PVR_POWER9))
		mtspr(SPRN_HMEER, mfspr(SPRN_HMEER) | PPC_BIT(17));

	if (xive_enabled())
		xive_smp_setup_cpu();
	else if (cpu != boot_cpuid)
		xics_setup_cpu();
}

static int pnv_smp_kick_cpu(int nr)
{
	unsigned int pcpu;
	unsigned long start_here =
			__pa(ppc_function_entry(generic_secondary_smp_init));
	long rc;
	uint8_t status;

	if (nr < 0 || nr >= nr_cpu_ids)
		return -EINVAL;

	pcpu = get_hard_smp_processor_id(nr);
	/*
	 * If we already started or OPAL is not supported, we just
	 * kick the CPU via the PACA
	 */
	if (paca_ptrs[nr]->cpu_start || !firmware_has_feature(FW_FEATURE_OPAL))
		goto kick;

	/*
	 * At this point, the CPU can either be spinning on the way in
	 * from kexec or be inside OPAL waiting to be started for the
	 * first time. OPAL v3 allows us to query OPAL to know if it
	 * has the CPUs, so we do that
	 */
	rc = opal_query_cpu_status(pcpu, &status);
	if (rc != OPAL_SUCCESS) {
		pr_warn("OPAL Error %ld querying CPU %d state\n", rc, nr);
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
		pr_devel("OPAL: Starting CPU %d (HW 0x%x)...\n", nr, pcpu);
		rc = opal_start_cpu(pcpu, start_here);
		if (rc != OPAL_SUCCESS) {
			pr_warn("OPAL Error %ld starting CPU %d\n", rc, nr);
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
	if (xive_enabled())
		xive_smp_disable_cpu();
	else
		xics_migrate_irqs_away();
	return 0;
}

static void pnv_flush_interrupts(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (xive_enabled())
			xive_flush_interrupt();
		else
			icp_opal_flush_interrupt();
	} else {
		icp_native_flush_interrupt();
	}
}

static void pnv_smp_cpu_kill_self(void)
{
	unsigned long srr1, unexpected_mask, wmask;
	unsigned int cpu;
	u64 lpcr_val;

	/* Standard hot unplug procedure */

	idle_task_exit();
	cpu = smp_processor_id();
	DBG("CPU%d offline\n", cpu);
	generic_set_cpu_dead(cpu);
	smp_wmb();

	wmask = SRR1_WAKEMASK;
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		wmask = SRR1_WAKEMASK_P8;

	/*
	 * This turns the irq soft-disabled state we're called with, into a
	 * hard-disabled state with pending irq_happened interrupts cleared.
	 *
	 * PACA_IRQ_DEC   - Decrementer should be ignored.
	 * PACA_IRQ_HMI   - Can be ignored, processing is done in real mode.
	 * PACA_IRQ_DBELL, EE, PMI - Unexpected.
	 */
	hard_irq_disable();
	if (generic_check_cpu_restart(cpu))
		goto out;

	unexpected_mask = ~(PACA_IRQ_DEC | PACA_IRQ_HMI | PACA_IRQ_HARD_DIS);
	if (local_paca->irq_happened & unexpected_mask) {
		if (local_paca->irq_happened & PACA_IRQ_EE)
			pnv_flush_interrupts();
		DBG("CPU%d Unexpected exit while offline irq_happened=%lx!\n",
				cpu, local_paca->irq_happened);
	}
	local_paca->irq_happened = PACA_IRQ_HARD_DIS;

	/*
	 * We don't want to take decrementer interrupts while we are
	 * offline, so clear LPCR:PECE1. We keep PECE2 (and
	 * LPCR_PECE_HVEE on P9) enabled so as to let IPIs in.
	 *
	 * If the CPU gets woken up by a special wakeup, ensure that
	 * the SLW engine sets LPCR with decrementer bit cleared, else
	 * the CPU will come back to the kernel due to a spurious
	 * wakeup.
	 */
	lpcr_val = mfspr(SPRN_LPCR) & ~(u64)LPCR_PECE1;
	pnv_program_cpu_hotplug_lpcr(cpu, lpcr_val);

	while (!generic_check_cpu_restart(cpu)) {
		/*
		 * Clear IPI flag, since we don't handle IPIs while
		 * offline, except for those when changing micro-threading
		 * mode, which are handled explicitly below, and those
		 * for coming online, which are handled via
		 * generic_check_cpu_restart() calls.
		 */
		kvmppc_clear_host_ipi(cpu);

		srr1 = pnv_cpu_offline(cpu);

		WARN_ON_ONCE(!irqs_disabled());
		WARN_ON(lazy_irq_pending());

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
		if (((srr1 & wmask) == SRR1_WAKEEE) ||
		    ((srr1 & wmask) == SRR1_WAKEHVI)) {
			pnv_flush_interrupts();
		} else if ((srr1 & wmask) == SRR1_WAKEHDBELL) {
			unsigned long msg = PPC_DBELL_TYPE(PPC_DBELL_SERVER);
			asm volatile(PPC_MSGCLR(%0) : : "r" (msg));
		} else if ((srr1 & wmask) == SRR1_WAKERESET) {
			irq_set_pending_from_srr1(srr1);
			/* Does not return */
		}

		smp_mb();

		/*
		 * For kdump kernels, we process the ipi and jump to
		 * crash_ipi_callback
		 */
		if (kdump_in_progress()) {
			/*
			 * If we got to this point, we've not used
			 * NMI's, otherwise we would have gone
			 * via the SRR1_WAKERESET path. We are
			 * using regular IPI's for waking up offline
			 * threads.
			 */
			struct pt_regs regs;

			ppc_save_regs(&regs);
			crash_ipi_callback(&regs);
			/* Does not return */
		}

		if (cpu_core_split_required())
			continue;

		if (srr1 && !generic_check_cpu_restart(cpu))
			DBG("CPU%d Unexpected exit while offline srr1=%lx!\n",
					cpu, srr1);

	}

	/*
	 * Re-enable decrementer interrupts in LPCR.
	 *
	 * Further, we want stop states to be woken up by decrementer
	 * for non-hotplug cases. So program the LPCR via stop api as
	 * well.
	 */
	lpcr_val = mfspr(SPRN_LPCR) | (u64)LPCR_PECE1;
	pnv_program_cpu_hotplug_lpcr(cpu, lpcr_val);
out:
	DBG("CPU%d coming online...\n", cpu);
}

#endif /* CONFIG_HOTPLUG_CPU */

static int pnv_cpu_bootable(unsigned int nr)
{
	/*
	 * Starting with POWER8, the subcore logic relies on all threads of a
	 * core being booted so that they can participate in split mode
	 * switches. So on those machines we ignore the smt_enabled_at_boot
	 * setting (smt-enabled on the kernel command line).
	 */
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		return 1;

	return smp_generic_cpu_bootable(nr);
}

static int pnv_smp_prepare_cpu(int cpu)
{
	if (xive_enabled())
		return xive_smp_prepare_cpu(cpu);
	return 0;
}

/* Cause IPI as setup by the interrupt controller (xics or xive) */
static void (*ic_cause_ipi)(int cpu);

static void pnv_cause_ipi(int cpu)
{
	if (doorbell_try_core_ipi(cpu))
		return;

	ic_cause_ipi(cpu);
}

static void __init pnv_smp_probe(void)
{
	if (xive_enabled())
		xive_smp_probe();
	else
		xics_smp_probe();

	if (cpu_has_feature(CPU_FTR_DBELL)) {
		ic_cause_ipi = smp_ops->cause_ipi;
		WARN_ON(!ic_cause_ipi);

		if (cpu_has_feature(CPU_FTR_ARCH_300))
			smp_ops->cause_ipi = doorbell_global_ipi;
		else
			smp_ops->cause_ipi = pnv_cause_ipi;
	}
}

static int pnv_system_reset_exception(struct pt_regs *regs)
{
	if (smp_handle_nmi_ipi(regs))
		return 1;
	return 0;
}

static int pnv_cause_nmi_ipi(int cpu)
{
	int64_t rc;

	if (cpu >= 0) {
		int h = get_hard_smp_processor_id(cpu);

		if (opal_check_token(OPAL_QUIESCE))
			opal_quiesce(QUIESCE_HOLD, h);

		rc = opal_signal_system_reset(h);

		if (opal_check_token(OPAL_QUIESCE))
			opal_quiesce(QUIESCE_RESUME, h);

		if (rc != OPAL_SUCCESS)
			return 0;
		return 1;

	} else if (cpu == NMI_IPI_ALL_OTHERS) {
		bool success = true;
		int c;

		if (opal_check_token(OPAL_QUIESCE))
			opal_quiesce(QUIESCE_HOLD, -1);

		/*
		 * We do not use broadcasts (yet), because it's not clear
		 * exactly what semantics Linux wants or the firmware should
		 * provide.
		 */
		for_each_online_cpu(c) {
			if (c == smp_processor_id())
				continue;

			rc = opal_signal_system_reset(
						get_hard_smp_processor_id(c));
			if (rc != OPAL_SUCCESS)
				success = false;
		}

		if (opal_check_token(OPAL_QUIESCE))
			opal_quiesce(QUIESCE_RESUME, -1);

		if (success)
			return 1;

		/*
		 * Caller will fall back to doorbells, which may pick
		 * up the remainders.
		 */
	}

	return 0;
}

static struct smp_ops_t pnv_smp_ops = {
	.message_pass	= NULL, /* Use smp_muxed_ipi_message_pass */
	.cause_ipi	= NULL,	/* Filled at runtime by pnv_smp_probe() */
	.cause_nmi_ipi	= NULL,
	.probe		= pnv_smp_probe,
	.prepare_cpu	= pnv_smp_prepare_cpu,
	.kick_cpu	= pnv_smp_kick_cpu,
	.setup_cpu	= pnv_smp_setup_cpu,
	.cpu_bootable	= pnv_cpu_bootable,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= pnv_smp_cpu_disable,
	.cpu_die	= generic_cpu_die,
#endif /* CONFIG_HOTPLUG_CPU */
};

/* This is called very early during platform setup_arch */
void __init pnv_smp_init(void)
{
	if (opal_check_token(OPAL_SIGNAL_SYSTEM_RESET)) {
		ppc_md.system_reset_exception = pnv_system_reset_exception;
		pnv_smp_ops.cause_nmi_ipi = pnv_cause_nmi_ipi;
	}
	smp_ops = &pnv_smp_ops;

#ifdef CONFIG_HOTPLUG_CPU
	ppc_md.cpu_die	= pnv_smp_cpu_kill_self;
#ifdef CONFIG_KEXEC_CORE
	crash_wake_offline = 1;
#endif
#endif
}
