/*
 *  Copyright (C) 2007 Lennert Buytenhek <buytenh@wantstofly.org>
 *  Copyright 2004-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Unused, we use CONFIG_MULTI_IRQ_HANDLER */

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	.endm

	.macro test_for_ipi, irqnr, irqstat, base, tmp
	.endm
