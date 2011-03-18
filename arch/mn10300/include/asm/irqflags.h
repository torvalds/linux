/* MN10300 IRQ flag handling
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

#include <asm/cpu-regs.h>
#ifndef __ASSEMBLY__
#include <linux/smp.h>
#endif

/*
 * interrupt control
 * - "disabled": run in IM1/2
 *   - level 0 - kernel debugger
 *   - level 1 - virtual serial DMA (if present)
 *   - level 5 - normal interrupt priority
 *   - level 6 - timer interrupt
 * - "enabled":  run in IM7
 */
#define MN10300_CLI_LEVEL	(CONFIG_LINUX_CLI_LEVEL << EPSW_IM_SHIFT)

#ifndef __ASSEMBLY__

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile("mov epsw,%0" : "=d"(flags));
	return flags;
}

static inline void arch_local_irq_disable(void)
{
	asm volatile(
		"	and %0,epsw	\n"
		"	or %1,epsw	\n"
		"	nop		\n"
		"	nop		\n"
		"	nop		\n"
		:
		: "i"(~EPSW_IM), "i"(EPSW_IE | MN10300_CLI_LEVEL)
		: "memory");
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;

	flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

/*
 * we make sure arch_irq_enable() doesn't cause priority inversion
 */
extern unsigned long __mn10300_irq_enabled_epsw[];

static inline void arch_local_irq_enable(void)
{
	unsigned long tmp;
	int cpu = raw_smp_processor_id();

	asm volatile(
		"	mov	epsw,%0		\n"
		"	and	%1,%0		\n"
		"	or	%2,%0		\n"
		"	mov	%0,epsw		\n"
		: "=&d"(tmp)
		: "i"(~EPSW_IM), "r"(__mn10300_irq_enabled_epsw[cpu])
		: "memory", "cc");
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	asm volatile(
		"	mov %0,epsw	\n"
		"	nop		\n"
		"	nop		\n"
		"	nop		\n"
		:
		: "d"(flags)
		: "memory", "cc");
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & (EPSW_IE | EPSW_IM)) != (EPSW_IE | EPSW_IM_7);
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

/*
 * Hook to save power by halting the CPU
 * - called from the idle loop
 * - must reenable interrupts (which takes three instruction cycles to complete)
 */
static inline void arch_safe_halt(void)
{
#ifdef CONFIG_SMP
	arch_local_irq_enable();
#else
	asm volatile(
		"	or	%0,epsw	\n"
		"	nop		\n"
		"	nop		\n"
		"	bset	%2,(%1)	\n"
		:
		: "i"(EPSW_IE|EPSW_IM), "n"(&CPUM), "i"(CPUM_SLEEP)
		: "cc");
#endif
}

#define __sleep_cpu()				\
do {						\
	asm volatile(				\
		"	bset	%1,(%0)\n"	\
		"1:	btst	%1,(%0)\n"	\
		"	bne	1b\n"		\
		:				\
		: "i"(&CPUM), "i"(CPUM_SLEEP)	\
		: "cc"				\
		);				\
} while (0)

static inline void arch_local_cli(void)
{
	asm volatile(
		"	and	%0,epsw		\n"
		"	nop			\n"
		"	nop			\n"
		"	nop			\n"
		:
		: "i"(~EPSW_IE)
		: "memory"
		);
}

static inline unsigned long arch_local_cli_save(void)
{
	unsigned long flags = arch_local_save_flags();
	arch_local_cli();
	return flags;
}

static inline void arch_local_sti(void)
{
	asm volatile(
		"	or	%0,epsw		\n"
		:
		: "i"(EPSW_IE)
		: "memory");
}

static inline void arch_local_change_intr_mask_level(unsigned long level)
{
	asm volatile(
		"	and	%0,epsw		\n"
		"	or	%1,epsw		\n"
		:
		: "i"(~EPSW_IM), "i"(EPSW_IE | level)
		: "cc", "memory");
}

#else /* !__ASSEMBLY__ */

#define LOCAL_SAVE_FLAGS(reg)			\
	mov	epsw,reg

#define LOCAL_IRQ_DISABLE				\
	and	~EPSW_IM,epsw;				\
	or	EPSW_IE|MN10300_CLI_LEVEL,epsw;		\
	nop;						\
	nop;						\
	nop

#define LOCAL_IRQ_ENABLE		\
	or	EPSW_IE|EPSW_IM_7,epsw

#define LOCAL_IRQ_RESTORE(reg)	\
	mov	reg,epsw

#define LOCAL_CLI_SAVE(reg)	\
	mov	epsw,reg;	\
	and	~EPSW_IE,epsw;	\
	nop;			\
	nop;			\
	nop

#define LOCAL_CLI		\
	and	~EPSW_IE,epsw;	\
	nop;			\
	nop;			\
	nop

#define LOCAL_STI		\
	or	EPSW_IE,epsw

#define LOCAL_CHANGE_INTR_MASK_LEVEL(level)	\
	and	~EPSW_IM,epsw;			\
	or	EPSW_IE|(level),epsw

#endif /* __ASSEMBLY__ */
#endif /* _ASM_IRQFLAGS_H */
