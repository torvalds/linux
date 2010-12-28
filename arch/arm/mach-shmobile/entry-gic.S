/*
 * ARM Interrupt demux handler using GIC
 *
 * Copyright (C) 2010 Magnus Damm
 * Copyright (C) 2010 Renesas Solutions Corp.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/entry-macro-multi.S>
#include <asm/hardware/gic.h>
#include <asm/hardware/entry-macro-gic.S>

	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =(0xf0000100)
	.endm

	arch_irq_handler shmobile_handle_irq_gic
