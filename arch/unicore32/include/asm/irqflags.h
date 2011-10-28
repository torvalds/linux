/*
 * linux/arch/unicore32/include/asm/irqflags.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_IRQFLAGS_H__
#define __UNICORE_IRQFLAGS_H__

#ifdef __KERNEL__

#include <asm/ptrace.h>

#define ARCH_IRQ_DISABLED	(PRIV_MODE | PSR_I_BIT)
#define ARCH_IRQ_ENABLED	(PRIV_MODE)

/*
 * Save the current interrupt enable state.
 */
static inline unsigned long arch_local_save_flags(void)
{
	unsigned long temp;

	asm volatile("mov %0, asr" : "=r" (temp) : : "memory", "cc");

	return temp & PSR_c;
}

/*
 * restore saved IRQ state
 */
static inline void arch_local_irq_restore(unsigned long flags)
{
	unsigned long temp;

	asm volatile(
		"mov	%0, asr\n"
		"mov.a	asr, %1\n"
		"mov.f	asr, %0"
		: "=&r" (temp)
		: "r" (flags)
		: "memory", "cc");
}

#include <asm-generic/irqflags.h>

#endif
#endif
