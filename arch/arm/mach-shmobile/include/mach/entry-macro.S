/*
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2008 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <mach/irqs.h>

#define INTCA_BASE	0xe6980000
#define INTFLGA_OFFS	0x00000018 /* accept pending interrupt */
#define INTEVTA_OFFS	0x00000020 /* vector number of accepted interrupt */
#define INTLVLA_OFFS	0x00000030 /* priority level of accepted interrupt */
#define INTLVLB_OFFS	0x00000034 /* previous priority level */

	.macro  disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr     \base, =INTCA_BASE
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
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
