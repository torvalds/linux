/*
 * arch/arm/mach-prima2/include/mach/entry-macro.S
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <mach/hardware.h>

#define SIRFSOC_INT_ID 0x38

	.macro  get_irqnr_preamble, base, tmp
	ldr     \base, =sirfsoc_intc_base
	ldr     \base, [\base]
	.endm

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr \irqnr, [\base, #SIRFSOC_INT_ID]	@ Get the highest priority irq
	cmp \irqnr, #0x40			@ the irq num can't be larger than 0x3f
	movges \irqnr, #0
	.endm
