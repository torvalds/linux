/*
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
#include <mach/hardware.h>
#include <mach/irqs.h>

	.macro	test_for_ipi, irqnr, irqstat, base, tmp
	.endm

	.macro	test_for_ltirq, irqnr, irqstat, base, tmp
	.endm

	.macro  disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr     \base, =INTFLGA
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr     \irqnr, [\base]
	cmp	\irqnr, #0
	beq	1000f
	/* intevt to irq number */
	lsr	\irqnr, \irqnr, #0x5
	subs	\irqnr, \irqnr, #16

1000:
	.endm
