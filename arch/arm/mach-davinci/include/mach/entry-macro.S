/*
 * Low-level IRQ helper macros for TI DaVinci-based platforms
 *
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 *
 * 2007 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <mach/io.h>
#include <mach/irqs.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		ldr \base, =IO_ADDRESS(DAVINCI_ARM_INTC_BASE)
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr \tmp, [\base, #0x14]
		mov \tmp, \tmp, lsr #2
		sub \irqnr, \tmp, #1
		cmp \tmp, #0
		.endm

		.macro	irq_prio_table
		.endm
