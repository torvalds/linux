/*
 *
 * arch-arm/mach-u300/include/mach/entry-macro.S
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * Low-level IRQ helper macros for ST-Ericsson U300
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <mach/hardware.h>
#include <asm/hardware/vic.h>

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\base, = U300_AHB_PER_VIRT_BASE-U300_AHB_PER_PHYS_BASE+U300_INTCON0_BASE
	ldr	\irqstat, [\base, #VIC_IRQ_STATUS] @ get masked status
	mov	\irqnr, #0
	teq	\irqstat, #0
	bne	1002f
1001:	ldr	\base, = U300_AHB_PER_VIRT_BASE-U300_AHB_PER_PHYS_BASE+U300_INTCON1_BASE
	ldr	\irqstat, [\base, #VIC_IRQ_STATUS] @ get masked status
	mov	\irqnr, #32
	teq	\irqstat, #0
	beq	1003f
1002:	tst	\irqstat, #1
	bne	1003f
	add	\irqnr, \irqnr, #1
	movs	\irqstat, \irqstat, lsr #1
	bne	1002b
1003:		/* EQ will be set if no irqs pending */
	.endm
