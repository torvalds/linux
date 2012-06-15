/*
 * linux/arch/arm/mach-mmp/include/mach/entry-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/irq.h>
#include <mach/regs-icu.h>

	.macro	get_irqnr_preamble, base, tmp
	mrc	p15, 0, \tmp, c0, c0, 0		@ CPUID
	and	\tmp, \tmp, #0xff00
	cmp	\tmp, #0x5800
	ldr	\base, =mmp_icu_base
	ldr	\base, [\base, #0]
	addne	\base, \base, #0x10c		@ PJ1 AP INT SEL register
	addeq	\base, \base, #0x104		@ PJ4 IRQ SEL register
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\tmp, [\base, #0]
	and	\irqnr, \tmp, #0x3f
	tst	\tmp, #(1 << 6)
	.endm
