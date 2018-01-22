/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCORE_IRQFLAGS_H
#define _ASM_SCORE_IRQFLAGS_H

#ifndef __ASSEMBLY__

#include <linux/types.h>

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile(
		"	mfcr	r8, cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	mv	%0, r8		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	ldi	r9, 0x1		\n"
		"	and	%0, %0, r9	\n"
		: "=r" (flags)
		:
		: "r8", "r9");
	return flags;
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	asm volatile(
		"	mfcr	r8, cr0		\n"
		"	li	r9, 0xfffffffe	\n"
		"	nop			\n"
		"	mv	%0, r8		\n"
		"	and	r8, r8, r9	\n"
		"	mtcr	r8, cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		: "=r" (flags)
		:
		: "r8", "r9", "memory");

	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile(
		"	mfcr	r8, cr0		\n"
		"	ldi	r9, 0x1		\n"
		"	and	%0, %0, r9	\n"
		"	or	r8, r8, %0	\n"
		"	mtcr	r8, cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		:
		: "r"(flags)
		: "r8", "r9", "memory");
}

static inline void arch_local_irq_enable(void)
{
	asm volatile(
		"	mfcr	r8,cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	ori	r8,0x1		\n"
		"	mtcr	r8,cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		:
		:
		: "r8", "memory");
}

static inline void arch_local_irq_disable(void)
{
	asm volatile(
		"	mfcr	r8,cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	srli	r8,r8,1		\n"
		"	slli	r8,r8,1		\n"
		"	mtcr	r8,cr0		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		:
		:
		: "r8", "memory");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & 1);
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_SCORE_IRQFLAGS_H */
