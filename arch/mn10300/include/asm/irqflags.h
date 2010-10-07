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

/*
 * interrupt control
 * - "disabled": run in IM1/2
 *   - level 0 - GDB stub
 *   - level 1 - virtual serial DMA (if present)
 *   - level 5 - normal interrupt priority
 *   - level 6 - timer interrupt
 * - "enabled":  run in IM7
 */
#ifdef CONFIG_MN10300_TTYSM
#define MN10300_CLI_LEVEL	EPSW_IM_2
#else
#define MN10300_CLI_LEVEL	EPSW_IM_1
#endif

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
extern unsigned long __mn10300_irq_enabled_epsw;

static inline void arch_local_irq_enable(void)
{
	unsigned long tmp;

	asm volatile(
		"	mov	epsw,%0		\n"
		"	and	%1,%0		\n"
		"	or	%2,%0		\n"
		"	mov	%0,epsw		\n"
		: "=&d"(tmp)
		: "i"(~EPSW_IM), "r"(__mn10300_irq_enabled_epsw)
		: "memory");
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
	return (flags & EPSW_IM) <= MN10300_CLI_LEVEL;
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
	asm volatile(
		"	or	%0,epsw	\n"
		"	nop		\n"
		"	nop		\n"
		"	bset	%2,(%1)	\n"
		:
		: "i"(EPSW_IE|EPSW_IM), "n"(&CPUM), "i"(CPUM_SLEEP)
		: "cc");
}

#endif /* __ASSEMBLY__ */
#endif /* _ASM_IRQFLAGS_H */
