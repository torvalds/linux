/*
 * arch/arm/mach-tegra/sleep.S
 *
 * Copyright (c) 2010-2011, NVIDIA Corporation.
 * Copyright (c) 2011, Google, Inc.
 *
 * Author: Colin Cross <ccross@android.com>
 *         Gary King <gking@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/cp15.h>

#include "iomap.h"

#include "flowctrl.h"
#include "sleep.h"

#ifdef CONFIG_PM_SLEEP
/*
 * tegra_disable_clean_inv_dcache
 *
 * disable, clean & invalidate the D-cache
 *
 * Corrupted registers: r1-r3, r6, r8, r9-r11
 */
ENTRY(tegra_disable_clean_inv_dcache)
	stmfd	sp!, {r0, r4-r5, r7, r9-r11, lr}
	dmb					@ ensure ordering

	/* Disable the D-cache */
	mrc	p15, 0, r2, c1, c0, 0
	bic	r2, r2, #CR_C
	mcr	p15, 0, r2, c1, c0, 0
	isb

	/* Flush the D-cache */
	bl	v7_flush_dcache_louis

	/* Trun off coherency */
	exit_smp r4, r5

	ldmfd	sp!, {r0, r4-r5, r7, r9-r11, pc}
ENDPROC(tegra_disable_clean_inv_dcache)

#endif
