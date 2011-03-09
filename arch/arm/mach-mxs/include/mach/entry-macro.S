/*
 * Low-level IRQ helper macros for Freescale MXS-based
 *
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <mach/mxs.h>

#define MXS_ICOLL_VBASE		MXS_IO_ADDRESS(MXS_ICOLL_BASE_ADDR)
#define HW_ICOLL_STAT_OFFSET	0x70

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqnr, [\base, #HW_ICOLL_STAT_OFFSET]
	cmp	\irqnr, #0x7F
	strne	\irqnr, [\base]
	moveqs	\irqnr, #0
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =MXS_ICOLL_VBASE
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm
