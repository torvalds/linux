#ifndef _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H
#define _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H

#define TRAMPOLINE_PHYS_LOW (0x467)
#define TRAMPOLINE_PHYS_HIGH (0x469)

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

#ifdef CONFIG_SMP
extern void __inquire_remote_apic(int apicid);
#else /* CONFIG_SMP */
static inline void __inquire_remote_apic(int apicid)
{
}
#endif /* CONFIG_SMP */

static inline void inquire_remote_apic(int apicid)
{
	if (apic_verbosity >= APIC_DEBUG)
		__inquire_remote_apic(apicid);
}

#endif /* _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H */
