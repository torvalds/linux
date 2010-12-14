/*
 * include/asm-arm/arch-tcc83x/entry-macro.S
 *
 * Author : <linux@telechips.com>
 * Created: June 10, 2008
 * Description: Low-level IRQ helper macros for Telechips-based platforms
 *
 * Copyright (C) 2008-2009 Telechips
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/hardware.h>
#include <mach/irqs.h>

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

		ldr	\base, =0xF2003000 @ base address of PIC registers

		@@ read MREQ register of PIC0

		mov	\irqnr, #0
		ldr	\irqstat, [\base, #0x00000014 ]	@ lower 32 interrupts
		cmp	\irqstat, #0
		bne	1001f

		@@ read MREQ register of PIC1

		ldr	\irqstat, [\base, #0x00000094]	@ upper 32 interrupts
		cmp	\irqstat, #0
		beq	1002f
		mov	\irqnr, #0x20

1001:
		movs	\tmp, \irqstat, lsl #16
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #16

		movs	\tmp, \irqstat, lsl #8
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #8

		movs	\tmp, \irqstat, lsl #4
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #4

		movs	\tmp, \irqstat, lsl #2
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #2

		movs	\tmp, \irqstat, lsl #1
		addeq	\irqnr, \irqnr, #1
		orrs	\base, \base, #1
1002:
		@@ exit here, Z flag unset if IRQ

	.endm
