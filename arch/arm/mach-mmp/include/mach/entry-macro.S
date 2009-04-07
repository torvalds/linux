/*
 * linux/arch/arm/mach-mmp/include/mach/entry-macro.S
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/regs-icu.h>

	.macro	disable_fiq
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =ICU_AP_IRQ_SEL_INT_NUM
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\tmp, [\base, #0]
	and	\irqnr, \tmp, #0x3f
	tst	\tmp, #(1 << 6)
	.endm
