/*
 * arch/arm/mach-spear6xx/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for SPEAr6xx machine family
 *
 * Copyright (C) 2009 ST Microelectronics
 * Rajeev Kumar<rajeev-dlh.kumar@st.com>
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
		ldr	\base, =VA_SPEAR6XX_CPU_VIC_PRI_BASE
		ldr	\irqstat, [\base, #VIC_IRQ_STATUS]	@ get status
		mov	\irqnr, #0
		teq	\irqstat, #0
		bne	1001f
		ldr	\base, =VA_SPEAR6XX_CPU_VIC_SEC_BASE
		ldr	\irqstat, [\base, #VIC_IRQ_STATUS]	@ get status
		teq	\irqstat, #0
		beq	1002f				@ this will set/reset
							@ zero register
		mov	\irqnr, #32
1001:
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

		rsb	\tmp, \tmp, #0x1F		@ tmp = 32 - tmp - 1
		add	\irqnr, \irqnr, \tmp

1002:		/* EQ will be set if no irqs pending */
		.endm
