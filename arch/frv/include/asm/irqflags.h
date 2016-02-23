/* FR-V interrupt handling
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

/*
 * interrupt flag manipulation
 * - use virtual interrupt management since touching the PSR is slow
 *   - ICC2.Z: T if interrupts virtually disabled
 *   - ICC2.C: F if interrupts really disabled
 * - if Z==1 upon interrupt:
 *   - C is set to 0
 *   - interrupts are really disabled
 *   - entry.S returns immediately
 * - uses TIHI (TRAP if Z==0 && C==0) #2 to really reenable interrupts
 *   - if taken, the trap:
 *     - sets ICC2.C
 *     - enables interrupts
 */
static inline void arch_local_irq_disable(void)
{
	/* set Z flag, but don't change the C flag */
	asm volatile("	andcc	gr0,gr0,gr0,icc2	\n"
		     :
		     :
		     : "memory", "icc2"
		     );
}

static inline void arch_local_irq_enable(void)
{
	/* clear Z flag and then test the C flag */
	asm volatile("  oricc	gr0,#1,gr0,icc2		\n"
		     "	tihi	icc2,gr0,#2		\n"
		     :
		     :
		     : "memory", "icc2"
		     );
}

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile("movsg ccr,%0"
		     : "=r"(flags)
		     :
		     : "memory");

	/* shift ICC2.Z to bit 0 */
	flags >>= 26;

	/* make flags 1 if interrupts disabled, 0 otherwise */
	return flags & 1UL;

}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	/* load the Z flag by turning 1 if disabled into 0 if disabled
	 * and thus setting the Z flag but not the C flag */
	asm volatile("  xoricc	%0,#1,gr0,icc2		\n"
		     /* then trap if Z=0 and C=0 */
		     "	tihi	icc2,gr0,#2		\n"
		     :
		     : "r"(flags)
		     : "memory", "icc2"
		     );

}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return flags;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

/*
 * real interrupt flag manipulation
 */
#define __arch_local_irq_disable()			\
do {							\
	unsigned long psr;				\
	asm volatile("	movsg	psr,%0		\n"	\
		     "	andi	%0,%2,%0	\n"	\
		     "	ori	%0,%1,%0	\n"	\
		     "	movgs	%0,psr		\n"	\
		     : "=r"(psr)			\
		     : "i" (PSR_PIL_14), "i" (~PSR_PIL)	\
		     : "memory");			\
} while (0)

#define __arch_local_irq_enable()			\
do {							\
	unsigned long psr;				\
	asm volatile("	movsg	psr,%0		\n"	\
		     "	andi	%0,%1,%0	\n"	\
		     "	movgs	%0,psr		\n"	\
		     : "=r"(psr)			\
		     : "i" (~PSR_PIL)			\
		     : "memory");			\
} while (0)

#define __arch_local_save_flags(flags)		\
do {						\
	typecheck(unsigned long, flags);	\
	asm("movsg psr,%0"			\
	    : "=r"(flags)			\
	    :					\
	    : "memory");			\
} while (0)

#define	__arch_local_irq_save(flags)			\
do {							\
	unsigned long npsr;				\
	typecheck(unsigned long, flags);		\
	asm volatile("	movsg	psr,%0		\n"	\
		     "	andi	%0,%3,%1	\n"	\
		     "	ori	%1,%2,%1	\n"	\
		     "	movgs	%1,psr		\n"	\
		     : "=r"(flags), "=r"(npsr)		\
		     : "i" (PSR_PIL_14), "i" (~PSR_PIL)	\
		     : "memory");			\
} while (0)

#define	__arch_local_irq_restore(flags)			\
do {							\
	typecheck(unsigned long, flags);		\
	asm volatile("	movgs	%0,psr		\n"	\
		     :					\
		     : "r" (flags)			\
		     : "memory");			\
} while (0)

#define __arch_irqs_disabled()			\
	((__get_PSR() & PSR_PIL) >= PSR_PIL_14)

#endif /* _ASM_IRQFLAGS_H */
