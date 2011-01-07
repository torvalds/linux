/*
 * ARM Interrupt demux handler using INTC
 *
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2008 Renesas Solutions Corp.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/entry-macro-multi.S>

#define INTCA_BASE	0xe6980000
#define INTFLGA_OFFS	0x00000018 /* accept pending interrupt */
#define INTEVTA_OFFS	0x00000020 /* vector number of accepted interrupt */
#define INTLVLA_OFFS	0x00000030 /* priority level of accepted interrupt */
#define INTLVLB_OFFS	0x00000034 /* previous priority level */

	.macro  get_irqnr_preamble, base, tmp
	ldr     \base, =INTCA_BASE
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	/* The single INTFLGA read access below results in the following:
	 *
	 * 1. INTLVLB is updated with old priority value from INTLVLA
	 * 2. Highest priority interrupt is accepted
	 * 3. INTLVLA is updated to contain priority of accepted interrupt
	 * 4. Accepted interrupt vector is stored in INTFLGA and INTEVTA
	 */
	ldr     \irqnr, [\base, #INTFLGA_OFFS]

	/* Restore INTLVLA with the value saved in INTLVLB.
	 * This is required to support interrupt priorities properly.
	 */
	ldrb	\tmp, [\base, #INTLVLB_OFFS]
	strb    \tmp, [\base, #INTLVLA_OFFS]

	/* Handle invalid vector number case */
	cmp	\irqnr, #0
	beq	1000f

	/* Convert vector to irq number, same as the evt2irq() macro */
	lsr	\irqnr, \irqnr, #0x5
	subs	\irqnr, \irqnr, #16

1000:
	.endm

	.macro  test_for_ipi, irqnr, irqstat, base, tmp
	.endm

	.macro  test_for_ltirq, irqnr, irqstat, base, tmp
	.endm

	arch_irq_handler shmobile_handle_irq_intc
