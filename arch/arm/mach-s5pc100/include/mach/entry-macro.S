/* arch/arm/mach-s5pc100/include/mach/entry-macro.S
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * Based on mach-s3c6400/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for the Samsung S5PC1XX series
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
*/

#include <asm/hardware/vic.h>
#include <mach/map.h>
#include <plat/irqs.h>

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =VA_VIC0
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

	@ check the vic0
	mov	\irqnr, # S5P_IRQ_OFFSET + 31
	ldr	\irqstat, [ \base, # VIC_IRQ_STATUS ]
	teq	\irqstat, #0

	@ otherwise try vic1
	addeq	\tmp, \base, #(VA_VIC1 - VA_VIC0)
	addeq	\irqnr, \irqnr, #32
	ldreq	\irqstat, [ \tmp, # VIC_IRQ_STATUS ]
	teqeq	\irqstat, #0

	@ otherwise try vic2
	addeq	\tmp, \base, #(VA_VIC2 - VA_VIC0)
	addeq	\irqnr, \irqnr, #32
	ldreq	\irqstat, [ \tmp, # VIC_IRQ_STATUS ]
	teqeq	\irqstat, #0

	clzne	\irqstat, \irqstat
	subne	\irqnr, \irqnr, \irqstat
	.endm
