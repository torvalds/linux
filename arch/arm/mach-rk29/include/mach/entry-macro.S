/* arch/arm/mach-rk29/include/mach/entry-macro.S
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "irqs.h"
#include "rk29_iomap.h"

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =RK29_GICPERI_BASE
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
#if 0	
	@ check the vic0
	mov	\irqnr, # S3C_IRQ_OFFSET + 31
	ldr	\irqstat, [ \base, # VIC_IRQ_STATUS ]
	teq	\irqstat, #0

	@ otherwise try vic1
	addeq	\tmp, \base, #(S3C_VA_VIC1 - S3C_VA_VIC0)
	addeq	\irqnr, \irqnr, #32
	ldreq	\irqstat, [ \tmp, # VIC_IRQ_STATUS ]
	teqeq	\irqstat, #0

	@ otherwise try vic2
	addeq	\tmp, \base, #(S3C_VA_VIC2 - S3C_VA_VIC0)
	addeq	\irqnr, \irqnr, #32
	ldreq	\irqstat, [ \tmp, # VIC_IRQ_STATUS ]
	teqeq	\irqstat, #0

	clzne	\irqstat, \irqstat
	subne	\irqnr, \irqnr, \irqstat
#endif		
	.endm