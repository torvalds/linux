#ifndef _ASM_X86_MACH_DEFAULT_MACH_IPI_H
#define _ASM_X86_MACH_DEFAULT_MACH_IPI_H

/* Avoid include hell */
#define NMI_VECTOR 0x02

void default_send_IPI_mask_bitmask(const struct cpumask *mask, int vector);
void default_send_IPI_mask_allbutself(const struct cpumask *mask, int vector);
void __default_send_IPI_shortcut(unsigned int shortcut, int vector);

extern int no_broadcast;

#ifdef CONFIG_X86_64
#include <asm/genapic.h>
#else
static inline void default_send_IPI_mask(const struct cpumask *mask, int vector)
{
	default_send_IPI_mask_bitmask(mask, vector);
}
void default_send_IPI_mask_allbutself(const struct cpumask *mask, int vector);
#endif

static inline void __default_local_send_IPI_allbutself(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		apic->send_IPI_mask_allbutself(cpu_online_mask, vector);
	else
		__default_send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void __default_local_send_IPI_all(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		apic->send_IPI_mask(cpu_online_mask, vector);
	else
		__default_send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

#ifdef CONFIG_X86_32
static inline void default_send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then we get an APIC send 
	 * error if we try to broadcast, thus avoid sending IPIs in this case.
	 */
	if (!(num_online_cpus() > 1))
		return;

	__default_local_send_IPI_allbutself(vector);
}

static inline void default_send_IPI_all(int vector)
{
	__default_local_send_IPI_all(vector);
}
#endif

#endif /* _ASM_X86_MACH_DEFAULT_MACH_IPI_H */
