/*
 * arch/arm/mach-dove/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for Marvell Dove platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/bridge-regs.h>

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =IRQ_VIRT_BASE
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	@ check low interrupts
	ldr	\irqstat, [\base, #IRQ_CAUSE_LOW_OFF]
	ldr	\tmp, [\base, #IRQ_MASK_LOW_OFF]
	mov	\irqnr, #32
	ands	\irqstat, \irqstat, \tmp

	@ if no low interrupts set, check high interrupts
	ldreq	\irqstat, [\base, #IRQ_CAUSE_HIGH_OFF]
	ldreq	\tmp, [\base, #IRQ_MASK_HIGH_OFF]
	moveq	\irqnr, #64
	andeqs	\irqstat, \irqstat, \tmp

	@ find first active interrupt source
	clzne	\irqstat, \irqstat
	subne	\irqnr, \irqnr, \irqstat
	.endm
