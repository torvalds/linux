/* arch/arm/mach-tegra/include/mach/entry-macro.S
 *
 * Copyright (C) 2009 Palm, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <mach/iomap.h>
#include <mach/io.h>

#define HAVE_GET_IRQNR_PREAMBLE
#include <asm/hardware/entry-macro-gic.S>

	/* Uses the GIC interrupt controller built into the cpu */
#define ICTRL_BASE (IO_CPU_VIRT + 0x100)

	.macro	disable_fiq
	.endm

	.macro	get_irqnr_preamble, base, tmp
	movw \base, #(ICTRL_BASE & 0x0000ffff)
	movt \base, #((ICTRL_BASE & 0xffff0000) >> 16)
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm
