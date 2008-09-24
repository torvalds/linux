/*
 * arch/arm/mach-imx/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for iMX-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_preamble, base, tmp
		.endm

		.macro	arch_ret_to_user, tmp1, tmp2
		.endm

#define AITC_NIVECSR   0x40
		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =IO_ADDRESS(IMX_AITC_BASE)
		@ Load offset & priority of the highest priority
		@ interrupt pending.
		ldr	\irqstat, [\base, #AITC_NIVECSR]
		@ Shift off the priority leaving the offset or
		@ "interrupt number", use arithmetic shift to
		@ transform illegal source (0xffff) as -1
		mov	\irqnr, \irqstat, asr #16
		adds	\tmp, \irqnr, #1
		.endm
