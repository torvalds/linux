/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <asm/hazards.h>

#if defined(CONFIG_CPU_MIPSR2) && !defined(CONFIG_MIPS_MT_SMTC)

__asm__(
	"	.macro	arch_local_irq_disable\n"
	"	.set	push						\n"
	"	.set	noat						\n"
	"	di							\n"
	"	irq_disable_hazard					\n"
	"	.set	pop						\n"
	"	.endm							\n");

static inline void arch_local_irq_disable(void)
{
	__asm__ __volatile__(
		"arch_local_irq_disable"
		: /* no outputs */
		: /* no inputs */
		: "memory");
}


__asm__(
	"	.macro	arch_local_irq_save result			\n"
	"	.set	push						\n"
	"	.set	reorder						\n"
	"	.set	noat						\n"
	"	di	\\result					\n"
	"	andi	\\result, 1					\n"
	"	irq_disable_hazard					\n"
	"	.set	pop						\n"
	"	.endm							\n");

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	asm volatile("arch_local_irq_save\t%0"
		     : "=r" (flags)
		     : /* no inputs */
		     : "memory");
	return flags;
}


__asm__(
	"	.macro	arch_local_irq_restore flags			\n"
	"	.set	push						\n"
	"	.set	noreorder					\n"
	"	.set	noat						\n"
#if defined(CONFIG_IRQ_CPU)
	/*
	 * Slow, but doesn't suffer from a relatively unlikely race
	 * condition we're having since days 1.
	 */
	"	beqz	\\flags, 1f					\n"
	"	di							\n"
	"	ei							\n"
	"1:								\n"
#else
	/*
	 * Fast, dangerous.  Life is fun, life is good.
	 */
	"	mfc0	$1, $12						\n"
	"	ins	$1, \\flags, 0, 1				\n"
	"	mtc0	$1, $12						\n"
#endif
	"	irq_disable_hazard					\n"
	"	.set	pop						\n"
	"	.endm							\n");

static inline void arch_local_irq_restore(unsigned long flags)
{
	unsigned long __tmp1;

	__asm__ __volatile__(
		"arch_local_irq_restore\t%0"
		: "=r" (__tmp1)
		: "0" (flags)
		: "memory");
}

static inline void __arch_local_irq_restore(unsigned long flags)
{
	unsigned long __tmp1;

	__asm__ __volatile__(
		"arch_local_irq_restore\t%0"
		: "=r" (__tmp1)
		: "0" (flags)
		: "memory");
}
#else
/* Functions that require preempt_{dis,en}able() are in mips-atomic.c */
void arch_local_irq_disable(void);
unsigned long arch_local_irq_save(void);
void arch_local_irq_restore(unsigned long flags);
void __arch_local_irq_restore(unsigned long flags);
#endif /* if defined(CONFIG_CPU_MIPSR2) && !defined(CONFIG_MIPS_MT_SMTC) */


__asm__(
	"	.macro	arch_local_irq_enable				\n"
	"	.set	push						\n"
	"	.set	reorder						\n"
	"	.set	noat						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	$1, $2, 1	# SMTC - clear TCStatus.IXMT	\n"
	"	ori	$1, 0x400					\n"
	"	xori	$1, 0x400					\n"
	"	mtc0	$1, $2, 1					\n"
#elif defined(CONFIG_CPU_MIPSR2)
	"	ei							\n"
#else
	"	mfc0	$1,$12						\n"
	"	ori	$1,0x1f						\n"
	"	xori	$1,0x1e						\n"
	"	mtc0	$1,$12						\n"
#endif
	"	irq_enable_hazard					\n"
	"	.set	pop						\n"
	"	.endm");

extern void smtc_ipi_replay(void);

static inline void arch_local_irq_enable(void)
{
#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * SMTC kernel needs to do a software replay of queued
	 * IPIs, at the cost of call overhead on each local_irq_enable()
	 */
	smtc_ipi_replay();
#endif
	__asm__ __volatile__(
		"arch_local_irq_enable"
		: /* no outputs */
		: /* no inputs */
		: "memory");
}


__asm__(
	"	.macro	arch_local_save_flags flags			\n"
	"	.set	push						\n"
	"	.set	reorder						\n"
#ifdef CONFIG_MIPS_MT_SMTC
	"	mfc0	\\flags, $2, 1					\n"
#else
	"	mfc0	\\flags, $12					\n"
#endif
	"	.set	pop						\n"
	"	.endm							\n");

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	asm volatile("arch_local_save_flags %0" : "=r" (flags));
	return flags;
}


static inline int arch_irqs_disabled_flags(unsigned long flags)
{
#ifdef CONFIG_MIPS_MT_SMTC
	/*
	 * SMTC model uses TCStatus.IXMT to disable interrupts for a thread/CPU
	 */
	return flags & 0x400;
#else
	return !(flags & 1);
#endif
}

#endif /* #ifndef __ASSEMBLY__ */

/*
 * Do the CPU's IRQ-state tracing from assembly code.
 */
#ifdef CONFIG_TRACE_IRQFLAGS
/* Reload some registers clobbered by trace_hardirqs_on */
#ifdef CONFIG_64BIT
# define TRACE_IRQS_RELOAD_REGS						\
	LONG_L	$11, PT_R11(sp);					\
	LONG_L	$10, PT_R10(sp);					\
	LONG_L	$9, PT_R9(sp);						\
	LONG_L	$8, PT_R8(sp);						\
	LONG_L	$7, PT_R7(sp);						\
	LONG_L	$6, PT_R6(sp);						\
	LONG_L	$5, PT_R5(sp);						\
	LONG_L	$4, PT_R4(sp);						\
	LONG_L	$2, PT_R2(sp)
#else
# define TRACE_IRQS_RELOAD_REGS						\
	LONG_L	$7, PT_R7(sp);						\
	LONG_L	$6, PT_R6(sp);						\
	LONG_L	$5, PT_R5(sp);						\
	LONG_L	$4, PT_R4(sp);						\
	LONG_L	$2, PT_R2(sp)
#endif
# define TRACE_IRQS_ON							\
	CLI;	/* make sure trace_hardirqs_on() is called in kernel level */ \
	jal	trace_hardirqs_on
# define TRACE_IRQS_ON_RELOAD						\
	TRACE_IRQS_ON;							\
	TRACE_IRQS_RELOAD_REGS
# define TRACE_IRQS_OFF							\
	jal	trace_hardirqs_off
#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_ON_RELOAD
# define TRACE_IRQS_OFF
#endif

#endif /* _ASM_IRQFLAGS_H */
