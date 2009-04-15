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
		ldr \base, =davinci_intc_base
		ldr \base, [\base]
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
#if defined(CONFIG_AINTC) && defined(CONFIG_CP_INTC)
		ldr \tmp, =davinci_intc_type
		ldr \tmp, [\tmp]
		cmp \tmp, #DAVINCI_INTC_TYPE_CP_INTC
		beq 1001f
#endif
#if defined(CONFIG_AINTC)
		ldr \tmp, [\base, #0x14]
		movs \tmp, \tmp, lsr #2
		sub \irqnr, \tmp, #1
		b 1002f
#endif
#if defined(CONFIG_CP_INTC)
1001:		ldr \irqnr, [\base, #0x80] /* get irq number */
		and \irqnr, \irqnr, #0xff  /* irq is in bits 0-9 */
		mov \tmp, \irqnr, lsr #3
		and \tmp, \tmp, #0xfc
		add \tmp, \tmp, #0x280 /* get the register offset */
		ldr \irqstat, [\base, \tmp] /* get the intc status */
		cmp \irqstat, #0x0
#endif
1002:
		.endm

		.macro	irq_prio_table
		.endm
