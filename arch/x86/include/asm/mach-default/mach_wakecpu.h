#ifndef _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H
#define _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H

static inline void default_wait_for_init_deassert(atomic_t *deassert)
{
	while (!atomic_read(deassert))
		cpu_relax();
	return;
}

#ifdef CONFIG_SMP
extern void __inquire_remote_apic(int apicid);
#else /* CONFIG_SMP */
static inline void __inquire_remote_apic(int apicid)
{
}
#endif /* CONFIG_SMP */

static inline void default_inquire_remote_apic(int apicid)
{
	if (apic_verbosity >= APIC_DEBUG)
		__inquire_remote_apic(apicid);
}

#endif /* _ASM_X86_MACH_DEFAULT_MACH_WAKECPU_H */
