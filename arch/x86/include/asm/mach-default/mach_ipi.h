#ifndef _ASM_X86_MACH_DEFAULT_MACH_IPI_H
#define _ASM_X86_MACH_DEFAULT_MACH_IPI_H

/* Avoid include hell */
#define NMI_VECTOR 0x02

void send_IPI_mask_bitmask(const struct cpumask *mask, int vector);
void send_IPI_mask_allbutself(const struct cpumask *mask, int vector);
void __send_IPI_shortcut(unsigned int shortcut, int vector);

extern int no_broadcast;

#ifdef CONFIG_X86_64
#include <asm/genapic.h>
#define send_IPI_mask (genapic->send_IPI_mask)
#define send_IPI_mask_allbutself (genapic->send_IPI_mask_allbutself)
#else
static inline void send_IPI_mask(const struct cpumask *mask, int vector)
{
	send_IPI_mask_bitmask(mask, vector);
}
void send_IPI_mask_allbutself(const struct cpumask *mask, int vector);
#endif

static inline void __local_send_IPI_allbutself(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		send_IPI_mask_allbutself(cpu_online_mask, vector);
	else
		__send_IPI_shortcut(APIC_DEST_ALLBUT, vector);
}

static inline void __local_send_IPI_all(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		send_IPI_mask(cpu_online_mask, vector);
	else
		__send_IPI_shortcut(APIC_DEST_ALLINC, vector);
}

#ifdef CONFIG_X86_64
#define send_IPI_allbutself (genapic->send_IPI_allbutself)
#define send_IPI_all (genapic->send_IPI_all)
#else
static inline void send_IPI_allbutself(int vector)
{
	/*
	 * if there are no other CPUs in the system then we get an APIC send 
	 * error if we try to broadcast, thus avoid sending IPIs in this case.
	 */
	if (!(num_online_cpus() > 1))
		return;

	__local_send_IPI_allbutself(vector);
	return;
}

static inline void send_IPI_all(int vector)
{
	__local_send_IPI_all(vector);
}
#endif

#endif /* _ASM_X86_MACH_DEFAULT_MACH_IPI_H */
