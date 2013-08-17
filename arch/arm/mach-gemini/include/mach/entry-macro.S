/*
 * Low-level IRQ helper macros for Gemini platform.
 *
 *  Copyright (C) 2001-2006 Storlink, Corp.
 *  Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>

#define IRQ_STATUS	0x14

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr     \irqstat, =IO_ADDRESS(GEMINI_INTERRUPT_BASE + IRQ_STATUS)
	ldr     \irqnr, [\irqstat]
	cmp     \irqnr, #0
	beq     2313f
	mov     \tmp, \irqnr
	mov     \irqnr, #0
2312:
	tst     \tmp, #1
	bne     2313f
	add     \irqnr, \irqnr, #1
	mov     \tmp, \tmp, lsr #1
	cmp     \irqnr, #31
	bcc     2312b
2313:
	.endm
