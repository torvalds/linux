/*
 * arch/arm/mach-spear3xx/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for SPEAr3xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/hardware/vic.h>
#include <mach/hardware.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_preamble, base, tmp
		.endm

		.macro	arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =VA_SPEAR3XX_ML1_VIC_BASE
		ldr	\irqstat, [\base, #VIC_IRQ_STATUS]	@ get status
		teq	\irqstat, #0
		beq	1001f				@ this will set/reset
							@ zero register
		/*
		 * Following code will find bit position of least significang
		 * bit set in irqstat, using following equation
		 * least significant bit set in n = (n & ~(n-1))
		 */
		sub	\tmp, \irqstat, #1		@ tmp = irqstat - 1
		mvn	\tmp, \tmp			@ tmp = ~tmp
		and	\irqstat, \irqstat, \tmp	@ irqstat &= tmp
		/* Now, irqstat is = bit no. of 1st bit set in vic irq status */
		clz	\tmp, \irqstat			@ tmp = leading zeros
		rsb	\irqnr, \tmp, #0x1F		@ irqnr = 32 - tmp - 1

1001:		/* EQ will be set if no irqs pending */
		.endm
