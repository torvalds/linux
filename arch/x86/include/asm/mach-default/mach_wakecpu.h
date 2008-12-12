#ifndef _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H
#define _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H

/* 
 * This file copes with machines that wakeup secondary CPUs by the
 * INIT, INIT, STARTUP sequence.
 */

#define WAKE_SECONDARY_VIA_INIT

#define TRAMPOLINE_LOW phys_to_virt(0x467)
#define TRAMPOLINE_HIGH phys_to_virt(0x469)

#define boot_cpu_apicid boot_cpu_physical_apicid

static inline void wait_for_init_deassert(atomic_t *deassert)
{
	while (!atomic_read(deassert))
		cpu_relax();
	return;
}

/* Nothing to do for most platforms, since cleared by the INIT cycle */
static inline void smp_callin_clear_local_apic(void)
{
}

static inline void store_NMI_vector(unsigned short *high, unsigned short *low)
{
}

static inline void restore_NMI_vector(unsigned short *high, unsigned short *low)
{
}

#define inquire_remote_apic(apicid) do {		\
		if (apic_verbosity >= APIC_DEBUG)	\
			__inquire_remote_apic(apicid);	\
	} while (0)

#endif /* _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H */
