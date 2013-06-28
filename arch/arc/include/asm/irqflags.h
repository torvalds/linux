/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARC_IRQFLAGS_H
#define __ASM_ARC_IRQFLAGS_H

/* vineetg: March 2010 : local_irq_save( ) optimisation
 *  -Remove explicit mov of current status32 into reg, that is not needed
 *  -Use BIC  insn instead of INVERTED + AND
 *  -Conditionally disable interrupts (if they are not enabled, don't disable)
*/

#ifdef __KERNEL__

#include <asm/arcregs.h>

#ifndef __ASSEMBLY__

/******************************************************************
 * IRQ Control Macros
 ******************************************************************/

/*
 * Save IRQ state and disable IRQs
 */
static inline long arch_local_irq_save(void)
{
	unsigned long temp, flags;

	__asm__ __volatile__(
	"	lr  %1, [status32]	\n"
	"	bic %0, %1, %2		\n"
	"	and.f 0, %1, %2	\n"
	"	flag.nz %0		\n"
	: "=r"(temp), "=r"(flags)
	: "n"((STATUS_E1_MASK | STATUS_E2_MASK))
	: "memory", "cc");

	return flags;
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{

	__asm__ __volatile__(
	"	flag %0			\n"
	:
	: "r"(flags)
	: "memory");
}

/*
 * Unconditionally Enable IRQs
 */
extern void arch_local_irq_enable(void);

/*
 * Unconditionally Disable IRQs
 */
static inline void arch_local_irq_disable(void)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	lr  %0, [status32]	\n"
	"	and %0, %0, %1		\n"
	"	flag %0			\n"
	: "=&r"(temp)
	: "n"(~(STATUS_E1_MASK | STATUS_E2_MASK))
	: "memory");
}

/*
 * save IRQ state
 */
static inline long arch_local_save_flags(void)
{
	unsigned long temp;

	__asm__ __volatile__(
	"	lr  %0, [status32]	\n"
	: "=&r"(temp)
	:
	: "memory");

	return temp;
}

/*
 * Query IRQ state
 */
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (STATUS_E1_MASK
#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
			| STATUS_E2_MASK
#endif
		));
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline void arch_mask_irq(unsigned int irq)
{
	unsigned int ienb;

	ienb = read_aux_reg(AUX_IENABLE);
	ienb &= ~(1 << irq);
	write_aux_reg(AUX_IENABLE, ienb);
}

static inline void arch_unmask_irq(unsigned int irq)
{
	unsigned int ienb;

	ienb = read_aux_reg(AUX_IENABLE);
	ienb |= (1 << irq);
	write_aux_reg(AUX_IENABLE, ienb);
}

#else

.macro IRQ_DISABLE  scratch
	lr	\scratch, [status32]
	bic	\scratch, \scratch, (STATUS_E1_MASK | STATUS_E2_MASK)
	flag	\scratch
.endm

.macro IRQ_DISABLE_SAVE  scratch, save
	lr	\scratch, [status32]
	mov	\save, \scratch		/* Make a copy */
	bic	\scratch, \scratch, (STATUS_E1_MASK | STATUS_E2_MASK)
	flag	\scratch
.endm

.macro IRQ_ENABLE  scratch
	lr	\scratch, [status32]
	or	\scratch, \scratch, (STATUS_E1_MASK | STATUS_E2_MASK)
	flag	\scratch
.endm

#endif	/* __ASSEMBLY__ */

#endif	/* KERNEL */

#endif
