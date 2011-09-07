/*
 * arch/arm/mach-lpc32xx/include/mach/entry-macro.S
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <mach/hardware.h>
#include <mach/platform.h>

#define LPC32XX_INTC_MASKED_STATUS_OFS	0x8

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =IO_ADDRESS(LPC32XX_MIC_BASE)
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

/*
 * Return IRQ number in irqnr. Also return processor Z flag status in CPSR
 * as set if an interrupt is pending.
 */
	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqstat, [\base, #LPC32XX_INTC_MASKED_STATUS_OFS]
	clz	\irqnr, \irqstat
	rsb	\irqnr, \irqnr, #31
	teq	\irqstat, #0
	.endm
