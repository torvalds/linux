#ifndef _ASM_X86_IPI_H
#define _ASM_X86_IPI_H

#ifdef CONFIG_X86_LOCAL_APIC

/*
 * Copyright 2004 James Cleverdon, IBM.
 * Subject to the GNU Public License, v.2
 *
 * Generic APIC InterProcessor Interrupt code.
 *
 * Moved to include file by James Cleverdon from
 * arch/x86-64/kernel/smp.c
 *
 * Copyrights from kernel/smp.c:
 *
 * (c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 * (c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 * (c) 2002,2003 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License, v.2
 */

#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/smp.h>

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static inline unsigned int __prepare_ICR(unsigned int shortcut, int vector,
					 unsigned int dest)
{
	unsigned int icr = shortcut | dest;

	switch (vector) {
	default:
		icr |= APIC_DM_FIXED | vector;
		break;
	case NMI_VECTOR:
		icr |= APIC_DM_NMI;
		break;
	}
	return icr;
}

static inline int __prepare_ICR2(unsigned int mask)
{
	return SET_APIC_DEST_FIELD(mask);
}

static inline void __xapic_wait_icr_idle(void)
{
	while (native_apic_mem_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

static inline void
__default_send_IPI_shortcut(unsigned int shortcut, int vector, unsigned int dest)
{
	/*
	 * Subtle. In the case of the 'never do double writes' workaround
	 * we have to lock out interrupts to be safe.  As we don't care
	 * of the value read we use an atomic rmw access to avoid costly
	 * cli/sti.  Otherwise we use an even cheaper single atomic write
	 * to the APIC.
	 */
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	__xapic_wait_icr_idle();

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector, dest);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	native_apic_mem_write(APIC_ICR, cfg);
}

/*
 * This is used to send an IPI with no shorthand notation (the destination is
 * specified in bits 56 to 63 of the ICR).
 */
static inline void
 __default_send_IPI_dest_field(unsigned int mask, int vector, unsigned int dest)
{
	unsigned long cfg;

	/*
	 * Wait for idle.
	 */
	if (unlikely(vector == NMI_VECTOR))
		safe_apic_wait_icr_idle();
	else
		__xapic_wait_icr_idle();

	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	native_apic_mem_write(APIC_ICR2, cfg);

	/*
	 * program the ICR
	 */
	cfg = __prepare_ICR(0, vector, dest);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	native_apic_mem_write(APIC_ICR, cfg);
}

extern void default_send_IPI_mask_sequence_phys(const struct cpumask *mask,
						 int vector);
extern void default_send_IPI_mask_allbutself_phys(const struct cpumask *mask,
							 int vector);

/* Avoid include hell */
#define NMI_VECTOR 0x02

extern int no_broadcast;

static inline void __default_local_send_IPI_allbutself(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		apic->send_IPI_mask_allbutself(cpu_online_mask, vector);
	else
		__default_send_IPI_shortcut(APIC_DEST_ALLBUT, vector, apic->dest_logical);
}

static inline void __default_local_send_IPI_all(int vector)
{
	if (no_broadcast || vector == NMI_VECTOR)
		apic->send_IPI_mask(cpu_online_mask, vector);
	else
		__default_send_IPI_shortcut(APIC_DEST_ALLINC, vector, apic->dest_logical);
}

#ifdef CONFIG_X86_32
extern void default_send_IPI_mask_sequence_logical(const struct cpumask *mask,
							 int vector);
extern void default_send_IPI_mask_allbutself_logical(const struct cpumask *mask,
							 int vector);
extern void default_send_IPI_mask_logical(const struct cpumask *mask,
						 int vector);
extern void default_send_IPI_allbutself(int vector);
extern void default_send_IPI_all(int vector);
extern void default_send_IPI_self(int vector);
#endif

#endif

#endif /* _ASM_X86_IPI_H */
