/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/arm/mach-lpc32xx/include/mach/entry-macro.S
 *
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2010 NXP Semiconductors
 */

#include <mach/hardware.h>
#include <mach/platform.h>

#define LPC32XX_INTC_MASKED_STATUS_OFS	0x8

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =IO_ADDRESS(LPC32XX_MIC_BASE)
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
