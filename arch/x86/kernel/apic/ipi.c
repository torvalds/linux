// SPDX-License-Identifier: GPL-2.0

#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/io_apic.h>

#include "local.h"

DEFINE_STATIC_KEY_FALSE(apic_use_ipi_shorthand);

#ifdef CONFIG_SMP
static int apic_ipi_shorthand_off __ro_after_init;

static __init int apic_ipi_shorthand(char *str)
{
	get_option(&str, &apic_ipi_shorthand_off);
	return 1;
}
__setup("no_ipi_broadcast=", apic_ipi_shorthand);

static int __init print_ipi_mode(void)
{
	pr_info("IPI shorthand broadcast: %s\n",
		apic_ipi_shorthand_off ? "disabled" : "enabled");
	return 0;
}
late_initcall(print_ipi_mode);

void apic_smt_update(void)
{
	/*
	 * Do not switch to broadcast mode if:
	 * - Disabled on the command line
	 * - Only a single CPU is online
	 * - Not all present CPUs have been at least booted once
	 *
	 * The latter is important as the local APIC might be in some
	 * random state and a broadcast might cause havoc. That's
	 * especially true for NMI broadcasting.
	 */
	if (apic_ipi_shorthand_off || num_online_cpus() == 1 ||
	    !cpumask_equal(cpu_present_mask, &cpus_booted_once_mask)) {
		static_branch_disable(&apic_use_ipi_shorthand);
	} else {
		static_branch_enable(&apic_use_ipi_shorthand);
	}
}

void apic_send_IPI_allbutself(unsigned int vector)
{
	if (num_online_cpus() < 2)
		return;

	if (static_branch_likely(&apic_use_ipi_shorthand))
		__apic_send_IPI_allbutself(vector);
	else
		__apic_send_IPI_mask_allbutself(cpu_online_mask, vector);
}

/*
 * Send a 'reschedule' IPI to another CPU. It goes straight through and
 * wastes no time serializing anything. Worst case is that we lose a
 * reschedule ...
 */
void native_smp_send_reschedule(int cpu)
{
	if (unlikely(cpu_is_offline(cpu))) {
		WARN(1, "sched: Unexpected reschedule of offline CPU#%d!\n", cpu);
		return;
	}
	__apic_send_IPI(cpu, RESCHEDULE_VECTOR);
}

void native_send_call_func_single_ipi(int cpu)
{
	__apic_send_IPI(cpu, CALL_FUNCTION_SINGLE_VECTOR);
}

void native_send_call_func_ipi(const struct cpumask *mask)
{
	if (static_branch_likely(&apic_use_ipi_shorthand)) {
		unsigned int cpu = smp_processor_id();

		if (!cpumask_or_equal(mask, cpumask_of(cpu), cpu_online_mask))
			goto sendmask;

		if (cpumask_test_cpu(cpu, mask))
			__apic_send_IPI_all(CALL_FUNCTION_VECTOR);
		else if (num_online_cpus() > 1)
			__apic_send_IPI_allbutself(CALL_FUNCTION_VECTOR);
		return;
	}

sendmask:
	__apic_send_IPI_mask(mask, CALL_FUNCTION_VECTOR);
}

void apic_send_nmi_to_offline_cpu(unsigned int cpu)
{
	if (WARN_ON_ONCE(!apic->nmi_to_offline_cpu))
		return;
	if (WARN_ON_ONCE(!cpumask_test_cpu(cpu, &cpus_booted_once_mask)))
		return;
	apic->send_IPI(cpu, NMI_VECTOR);
}
#endif /* CONFIG_SMP */

static inline int __prepare_ICR2(unsigned int mask)
{
	return SET_XAPIC_DEST_FIELD(mask);
}

u32 apic_mem_wait_icr_idle_timeout(void)
{
	int cnt;

	for (cnt = 0; cnt < 1000; cnt++) {
		if (!(apic_read(APIC_ICR) & APIC_ICR_BUSY))
			return 0;
		inc_irq_stat(icr_read_retry_count);
		udelay(100);
	}
	return APIC_ICR_BUSY;
}

void apic_mem_wait_icr_idle(void)
{
	while (native_apic_mem_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

/*
 * This is safe against interruption because it only writes the lower 32
 * bits of the APIC_ICR register. The destination field is ignored for
 * short hand IPIs.
 *
 *  wait_icr_idle()
 *  write(ICR2, dest)
 *  NMI
 *	wait_icr_idle()
 *	write(ICR)
 *	wait_icr_idle()
 *  write(ICR)
 *
 * This function does not need to disable interrupts as there is no ICR2
 * interaction. The memory write is direct except when the machine is
 * affected by the 11AP Pentium erratum, which turns the plain write into
 * an XCHG operation.
 */
static void __default_send_IPI_shortcut(unsigned int shortcut, int vector)
{
	/*
	 * Wait for the previous ICR command to complete.  Use
	 * safe_apic_wait_icr_idle() for the NMI vector as there have been
	 * issues where otherwise the system hangs when the panic CPU tries
	 * to stop the others before launching the kdump kernel.
	 */
	if (unlikely(vector == NMI_VECTOR))
		apic_mem_wait_icr_idle_timeout();
	else
		apic_mem_wait_icr_idle();

	/* Destination field (ICR2) and the destination mode are ignored */
	native_apic_mem_write(APIC_ICR, __prepare_ICR(shortcut, vector, 0));
}

/*
 * This is used to send an IPI with no shorthand notation (the destination is
 * specified in bits 56 to 63 of the ICR).
 */
void __default_send_IPI_dest_field(unsigned int dest_mask, int vector,
				   unsigned int dest_mode)
{
	/* See comment in __default_send_IPI_shortcut() */
	if (unlikely(vector == NMI_VECTOR))
		apic_mem_wait_icr_idle_timeout();
	else
		apic_mem_wait_icr_idle();

	/* Set the IPI destination field in the ICR */
	native_apic_mem_write(APIC_ICR2, __prepare_ICR2(dest_mask));
	/* Send it with the proper destination mode */
	native_apic_mem_write(APIC_ICR, __prepare_ICR(0, vector, dest_mode));
}

void default_send_IPI_single_phys(int cpu, int vector)
{
	unsigned long flags;

	local_irq_save(flags);
	__default_send_IPI_dest_field(per_cpu(x86_cpu_to_apicid, cpu),
				      vector, APIC_DEST_PHYSICAL);
	local_irq_restore(flags);
}

void default_send_IPI_mask_sequence_phys(const struct cpumask *mask, int vector)
{
	unsigned long flags;
	unsigned long cpu;

	local_irq_save(flags);
	for_each_cpu(cpu, mask) {
		__default_send_IPI_dest_field(per_cpu(x86_cpu_to_apicid,
				cpu), vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

void default_send_IPI_mask_allbutself_phys(const struct cpumask *mask,
						 int vector)
{
	unsigned int cpu, this_cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);
	for_each_cpu(cpu, mask) {
		if (cpu == this_cpu)
			continue;
		__default_send_IPI_dest_field(per_cpu(x86_cpu_to_apicid,
				 cpu), vector, APIC_DEST_PHYSICAL);
	}
	local_irq_restore(flags);
}

/*
 * Helper function for APICs which insist on cpumasks
 */
void default_send_IPI_single(int cpu, int vector)
{
	__apic_send_IPI_mask(cpumask_of(cpu), vector);
}

void default_send_IPI_allbutself(int vector)
{
	__default_send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

void default_send_IPI_all(int vector)
{
	__default_send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

void default_send_IPI_self(int vector)
{
	__default_send_IPI_shortcut(APIC_DEST_SELF, vector);
}

#ifdef CONFIG_X86_32
void default_send_IPI_mask_sequence_logical(const struct cpumask *mask, int vector)
{
	unsigned long flags;
	unsigned int cpu;

	local_irq_save(flags);
	for_each_cpu(cpu, mask)
		__default_send_IPI_dest_field(1U << cpu, vector, APIC_DEST_LOGICAL);
	local_irq_restore(flags);
}

void default_send_IPI_mask_allbutself_logical(const struct cpumask *mask,
						 int vector)
{
	unsigned int cpu, this_cpu = smp_processor_id();
	unsigned long flags;

	local_irq_save(flags);
	for_each_cpu(cpu, mask) {
		if (cpu == this_cpu)
			continue;
		__default_send_IPI_dest_field(1U << cpu, vector, APIC_DEST_LOGICAL);
	}
	local_irq_restore(flags);
}

void default_send_IPI_mask_logical(const struct cpumask *cpumask, int vector)
{
	unsigned long mask = cpumask_bits(cpumask)[0];
	unsigned long flags;

	if (!mask)
		return;

	local_irq_save(flags);
	WARN_ON(mask & ~cpumask_bits(cpu_online_mask)[0]);
	__default_send_IPI_dest_field(mask, vector, APIC_DEST_LOGICAL);
	local_irq_restore(flags);
}

#ifdef CONFIG_SMP
static int convert_apicid_to_cpu(u32 apic_id)
{
	int i;

	for_each_possible_cpu(i) {
		if (per_cpu(x86_cpu_to_apicid, i) == apic_id)
			return i;
	}
	return -1;
}

int safe_smp_processor_id(void)
{
	u32 apicid;
	int cpuid;

	if (!boot_cpu_has(X86_FEATURE_APIC))
		return 0;

	apicid = read_apic_id();
	if (apicid == BAD_APICID)
		return 0;

	cpuid = convert_apicid_to_cpu(apicid);

	return cpuid >= 0 ? cpuid : 0;
}
#endif
#endif
