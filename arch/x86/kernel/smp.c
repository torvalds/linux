// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@lxorguk.ukuu.org.uk>
 *	(c) 1998-99, 2000, 2009 Ingo Molnar <mingo@redhat.com>
 *      (c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	i386 and x86_64 integration by Glauber Costa <gcosta@redhat.com>
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/gfp.h>

#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/apic.h>
#include <asm/cpu.h>
#include <asm/idtentry.h>
#include <asm/nmi.h>
#include <asm/mce.h>
#include <asm/trace/irq_vectors.h>
#include <asm/kexec.h>
#include <asm/reboot.h>

/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP errata are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP errata are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP errata are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deasserting IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesn't have the work
 *	arounds. Basically that's so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

static atomic_t stopping_cpu = ATOMIC_INIT(-1);
static bool smp_no_nmi_ipi = false;

static int smp_stop_nmi_callback(unsigned int val, struct pt_regs *regs)
{
	/* We are registered on stopping cpu too, avoid spurious NMI */
	if (raw_smp_processor_id() == atomic_read(&stopping_cpu))
		return NMI_HANDLED;

	cpu_emergency_disable_virtualization();
	stop_this_cpu(NULL);

	return NMI_HANDLED;
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_reboot)
{
	ack_APIC_irq();
	cpu_emergency_disable_virtualization();
	stop_this_cpu(NULL);
}

static int register_stop_handler(void)
{
	return register_nmi_handler(NMI_LOCAL, smp_stop_nmi_callback,
				    NMI_FLAG_FIRST, "smp_stop");
}

static void native_stop_other_cpus(int wait)
{
	unsigned int cpu = smp_processor_id();
	unsigned long flags, timeout;

	if (reboot_force)
		return;

	/* Only proceed if this is the first CPU to reach this code */
	if (atomic_cmpxchg(&stopping_cpu, -1, cpu) != -1)
		return;

	/*
	 * 1) Send an IPI on the reboot vector to all other CPUs.
	 *
	 *    The other CPUs should react on it after leaving critical
	 *    sections and re-enabling interrupts. They might still hold
	 *    locks, but there is nothing which can be done about that.
	 *
	 * 2) Wait for all other CPUs to report that they reached the
	 *    HLT loop in stop_this_cpu()
	 *
	 * 3) If #2 timed out send an NMI to the CPUs which did not
	 *    yet report
	 *
	 * 4) Wait for all other CPUs to report that they reached the
	 *    HLT loop in stop_this_cpu()
	 *
	 * #3 can obviously race against a CPU reaching the HLT loop late.
	 * That CPU will have reported already and the "have all CPUs
	 * reached HLT" condition will be true despite the fact that the
	 * other CPU is still handling the NMI. Again, there is no
	 * protection against that as "disabled" APICs still respond to
	 * NMIs.
	 */
	cpumask_copy(&cpus_stop_mask, cpu_online_mask);
	cpumask_clear_cpu(cpu, &cpus_stop_mask);

	if (!cpumask_empty(&cpus_stop_mask)) {
		apic_send_IPI_allbutself(REBOOT_VECTOR);

		/*
		 * Don't wait longer than a second for IPI completion. The
		 * wait request is not checked here because that would
		 * prevent an NMI shutdown attempt in case that not all
		 * CPUs reach shutdown state.
		 */
		timeout = USEC_PER_SEC;
		while (!cpumask_empty(&cpus_stop_mask) && timeout--)
			udelay(1);
	}

	/* if the REBOOT_VECTOR didn't work, try with the NMI */
	if (!cpumask_empty(&cpus_stop_mask)) {
		/*
		 * If NMI IPI is enabled, try to register the stop handler
		 * and send the IPI. In any case try to wait for the other
		 * CPUs to stop.
		 */
		if (!smp_no_nmi_ipi && !register_stop_handler()) {
			pr_emerg("Shutting down cpus with NMI\n");

			for_each_cpu(cpu, &cpus_stop_mask)
				apic->send_IPI(cpu, NMI_VECTOR);
		}
		/*
		 * Don't wait longer than 10 ms if the caller didn't
		 * request it. If wait is true, the machine hangs here if
		 * one or more CPUs do not reach shutdown state.
		 */
		timeout = USEC_PER_MSEC * 10;
		while (!cpumask_empty(&cpus_stop_mask) && (wait || timeout--))
			udelay(1);
	}

	local_irq_save(flags);
	disable_local_APIC();
	mcheck_cpu_clear(this_cpu_ptr(&cpu_info));
	local_irq_restore(flags);

	/*
	 * Ensure that the cpus_stop_mask cache lines are invalidated on
	 * the other CPUs. See comment vs. SME in stop_this_cpu().
	 */
	cpumask_clear(&cpus_stop_mask);
}

/*
 * Reschedule call back. KVM uses this interrupt to force a cpu out of
 * guest mode.
 */
DEFINE_IDTENTRY_SYSVEC_SIMPLE(sysvec_reschedule_ipi)
{
	ack_APIC_irq();
	trace_reschedule_entry(RESCHEDULE_VECTOR);
	inc_irq_stat(irq_resched_count);
	scheduler_ipi();
	trace_reschedule_exit(RESCHEDULE_VECTOR);
}

DEFINE_IDTENTRY_SYSVEC(sysvec_call_function)
{
	ack_APIC_irq();
	trace_call_function_entry(CALL_FUNCTION_VECTOR);
	inc_irq_stat(irq_call_count);
	generic_smp_call_function_interrupt();
	trace_call_function_exit(CALL_FUNCTION_VECTOR);
}

DEFINE_IDTENTRY_SYSVEC(sysvec_call_function_single)
{
	ack_APIC_irq();
	trace_call_function_single_entry(CALL_FUNCTION_SINGLE_VECTOR);
	inc_irq_stat(irq_call_count);
	generic_smp_call_function_single_interrupt();
	trace_call_function_single_exit(CALL_FUNCTION_SINGLE_VECTOR);
}

static int __init nonmi_ipi_setup(char *str)
{
	smp_no_nmi_ipi = true;
	return 1;
}

__setup("nonmi_ipi", nonmi_ipi_setup);

struct smp_ops smp_ops = {
	.smp_prepare_boot_cpu	= native_smp_prepare_boot_cpu,
	.smp_prepare_cpus	= native_smp_prepare_cpus,
	.smp_cpus_done		= native_smp_cpus_done,

	.stop_other_cpus	= native_stop_other_cpus,
#if defined(CONFIG_KEXEC_CORE)
	.crash_stop_other_cpus	= kdump_nmi_shootdown_cpus,
#endif
	.smp_send_reschedule	= native_smp_send_reschedule,

	.cpu_up			= native_cpu_up,
	.cpu_die		= native_cpu_die,
	.cpu_disable		= native_cpu_disable,
	.play_dead		= native_play_dead,

	.send_call_func_ipi	= native_send_call_func_ipi,
	.send_call_func_single_ipi = native_send_call_func_single_ipi,
};
EXPORT_SYMBOL_GPL(smp_ops);
