/* arch/arm/include/asm/entry-macro-vic2.S
 *
 * Originally arch/arm/mach-s3c6400/include/mach/entry-macro.S
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Low-level IRQ helper macros for a device with two VICs
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
*/

/* This should be included from <mach/entry-macro.S> with the necessary
 * defines for virtual addresses and IRQ bases for the two vics.
 *
 * The code needs the following defined:
 *	IRQ_VIC0_BASE	IRQ number of VIC0's first IRQ
 *	IRQ_VIC1_BASE	IRQ number of VIC1's first IRQ
 *	VA_VIC0		Virtual address of VIC0
 *	VA_VIC1		Virtual address of VIC1
 *
 * Note, code assumes VIC0's virtual address is an ARM immediate constant
 * away from VIC1.
*/

#include <asm/hardware/vic.h>

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =VA_VIC0
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

	@ check the vic0
	mov	\irqnr, #IRQ_VIC0_BASE + 31
	ldr	\irqstat, [ \base, # VIC_IRQ_STATUS ]
	teq	\irqstat, #0

	@ otherwise try vic1
	addeq	\tmp, \base, #(VA_VIC1 - VA_VIC0)
	addeq	\irqnr, \irqnr, #(IRQ_VIC1_BASE - IRQ_VIC0_BASE)
	ldreq	\irqstat, [ \tmp, # VIC_IRQ_STATUS ]
	teqeq	\irqstat, #0

	clzne	\irqstat, \irqstat
	subne	\irqnr, \irqnr, \irqstat
	.endm
