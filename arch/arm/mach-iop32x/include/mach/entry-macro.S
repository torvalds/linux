/*
 * arch/arm/mach-iop32x/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP32x-based platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/iop32x.h>

	.macro get_irqnr_preamble, base, tmp
	mrc	p15, 0, \tmp, c15, c1, 0
	orr	\tmp, \tmp, #(1 << 6)
	mcr	p15, 0, \tmp, c15, c1, 0	@ Enable cp6 access
	mrc	p15, 0, \tmp, c15, c1, 0
	mov	\tmp, \tmp
	sub	pc, pc, #4			@ cp_wait
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	mrc     p6, 0, \irqstat, c8, c0, 0	@ Read IINTSRC
	cmp     \irqstat, #0
	clzne   \irqnr, \irqstat
	rsbne   \irqnr, \irqnr, #31
	.endm

	.macro arch_ret_to_user, tmp1, tmp2
	mrc	p15, 0, \tmp1, c15, c1, 0
	ands	\tmp2, \tmp1, #(1 << 6)
	bicne	\tmp1, \tmp1, #(1 << 6)
	mcrne	p15, 0, \tmp1, c15, c1, 0	@ Disable cp6 access
	.endm
