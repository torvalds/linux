/*
 * Low-level IRQ helper macros for U8500 platforms
 *
 * Copyright (C) 2009 ST-Ericsson.
 *
 * This file is a copy of ARM Realview platform.
 *	-just satisfied checkpatch script.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#define HAVE_GET_IRQNR_PREAMBLE
#include <asm/hardware/entry-macro-gic.S>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		ldr     \base, =IO_ADDRESS(UX500_GIC_CPU_BASE)
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm
