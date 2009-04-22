/*
 * Low-level IRQ helper macros for Freescale STMP378X
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

	        mov	\base, #0xf0000000	@ vm address of IRQ controller
		ldr	\irqnr, [\base, #0x70]	@ HW_ICOLL_STAT
		cmp	\irqnr, #0x7f
		moveqs	\irqnr, #0		@ Zero flag set for no IRQ

		.endm

                .macro  get_irqnr_preamble, base, tmp
                .endm

                .macro  arch_ret_to_user, tmp1, tmp2
                .endm
