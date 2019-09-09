/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  C6X IRQ flag handling
 *
 * Copyright (C) 2010 Texas Instruments Incorporated
 * Written by Mark Salter (msalter@redhat.com)
 */

#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__

/* read interrupt enabled status */
static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile (" mvc .s2 CSR,%0\n" : "=b"(flags));
	return flags;
}

/* set interrupt enabled status */
static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile (" mvc .s2 %0,CSR\n" : : "b"(flags) : "memory");
}

/* unconditionally enable interrupts */
static inline void arch_local_irq_enable(void)
{
	unsigned long flags = arch_local_save_flags();
	flags |= 1;
	arch_local_irq_restore(flags);
}

/* unconditionally disable interrupts */
static inline void arch_local_irq_disable(void)
{
	unsigned long flags = arch_local_save_flags();
	flags &= ~1;
	arch_local_irq_restore(flags);
}

/* get status and disable interrupts */
static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_restore(flags & ~1);
	return flags;
}

/* test flags */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & 1) == 0;
}

/* test hardware interrupt enable bit */
static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* __ASSEMBLY__ */
#endif /* __ASM_IRQFLAGS_H */
