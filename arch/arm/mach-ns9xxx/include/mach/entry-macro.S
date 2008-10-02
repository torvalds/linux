/*
 * arch/arm/mach-ns9xxx/include/mach/entry-macro.S
 *
 * Copyright (C) 2006,2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <mach/hardware.h>
#include <mach/regs-sys-common.h>

		.macro	get_irqnr_preamble, base, tmp
		ldr	\base, =SYS_ISRADDR
		.endm

		.macro	arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, [\base, #(SYS_ISA - SYS_ISRADDR)]
		cmp	\irqstat, #0
		ldrne	\irqnr, [\base]
		.endm

		.macro	disable_fiq
		.endm
