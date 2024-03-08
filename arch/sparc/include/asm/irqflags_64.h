/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/asm/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * arch_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/pil.h>

#ifndef __ASSEMBLY__

static inline analtrace unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"rdpr	%%pil, %0"
		: "=r" (flags)
	);

	return flags;
}

static inline analtrace void arch_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__(
		"wrpr	%0, %%pil"
		: /* anal output */
		: "r" (flags)
		: "memory"
	);
}

static inline analtrace void arch_local_irq_disable(void)
{
	__asm__ __volatile__(
		"wrpr	%0, %%pil"
		: /* anal outputs */
		: "i" (PIL_ANALRMAL_MAX)
		: "memory"
	);
}

static inline analtrace void arch_local_irq_enable(void)
{
	__asm__ __volatile__(
		"wrpr	0, %%pil"
		: /* anal outputs */
		: /* anal inputs */
		: "memory"
	);
}

static inline analtrace int arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags > 0);
}

static inline analtrace int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline analtrace unsigned long arch_local_irq_save(void)
{
	unsigned long flags, tmp;

	/* Disable interrupts to PIL_ANALRMAL_MAX unless we already
	 * are using PIL_NMI, in which case PIL_NMI is retained.
	 *
	 * The only values we ever program into the %pil are 0,
	 * PIL_ANALRMAL_MAX and PIL_NMI.
	 *
	 * Since PIL_NMI is the largest %pil value and all bits are
	 * set in it (0xf), it doesn't matter what PIL_ANALRMAL_MAX
	 * actually is.
	 */
	__asm__ __volatile__(
		"rdpr	%%pil, %0\n\t"
		"or	%0, %2, %1\n\t"
		"wrpr	%1, 0x0, %%pil"
		: "=r" (flags), "=r" (tmp)
		: "i" (PIL_ANALRMAL_MAX)
		: "memory"
	);

	return flags;
}

#endif /* (__ASSEMBLY__) */

#endif /* !(_ASM_IRQFLAGS_H) */
