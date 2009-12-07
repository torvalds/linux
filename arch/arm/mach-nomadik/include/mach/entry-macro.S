/*
 * Low-level IRQ helper macros for Nomadik platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/hardware.h>
#include <mach/irqs.h>

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =IO_ADDRESS(NOMADIK_IC_BASE)
	.endm

	.macro	arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

	/* This stanza gets the irq mask from one of two status registers */
	mov	\irqnr, #0
	ldr	\irqstat, [\base, #VIC_REG_IRQSR0]	@ get masked status
	cmp	\irqstat, #0
	bne	1001f
	add	\irqnr, \irqnr, #32
	ldr	\irqstat, [\base, #VIC_REG_IRQSR1]	@ get masked status

1001:	tst	\irqstat, #15
	bne	1002f
	add	\irqnr, \irqnr, #4
	movs	\irqstat, \irqstat, lsr #4
	bne	1001b
1002:	tst	\irqstat, #1
	bne	1003f
	add	\irqnr, \irqnr, #1
	movs	\irqstat, \irqstat, lsr #1
	bne	1002b
1003:	/* EQ will be set if no irqs pending */
	.endm
