/*
 * arch/arm/mach-loki/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for Marvell Loki (88RC8480) platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/loki.h>

	.macro  disable_fiq
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =IRQ_VIRT_BASE
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqstat, [\base, #IRQ_CAUSE_OFF]
	ldr	\tmp, [\base, #IRQ_MASK_OFF]
	mov	\irqnr, #0
	ands	\irqstat, \irqstat, \tmp
	clzne	\irqnr, \irqstat
	rsbne	\irqnr, \irqnr, #31
	.endm
