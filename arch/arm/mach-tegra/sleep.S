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
#include <asm/cache.h>
#include <asm/cp15.h>
#include <asm/hardware/cache-l2x0.h>

#include "iomap.h"
#include "sleep.h"

#define CLK_RESET_CCLK_BURST	0x20
#define CLK_RESET_CCLK_DIVIDER  0x24

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_PM_SLEEP)
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
	cmp	r0, #TEGRA_FLUSH_CACHE_ALL
	blne	v7_flush_dcache_louis
	bleq	v7_flush_dcache_all

	/* Trun off coherency */
	exit_smp r4, r5

	ldmfd	sp!, {r0, r4-r5, r7, r9-r11, pc}
ENDPROC(tegra_disable_clean_inv_dcache)
#endif

#ifdef CONFIG_PM_SLEEP
/*
 * tegra_init_l2_for_a15
 *
 * set up the correct L2 cache data RAM latency
 */
ENTRY(tegra_init_l2_for_a15)
	mrc	p15, 0, r0, c0, c0, 5
	ubfx	r0, r0, #8, #4
	tst	r0, #1				@ only need for cluster 0
	bne	_exit_init_l2_a15

	mrc	p15, 0x1, r0, c9, c0, 2
	and	r0, r0, #7
	cmp	r0, #2
	bicne	r0, r0, #7
	orrne	r0, r0, #2
	mcrne	p15, 0x1, r0, c9, c0, 2
_exit_init_l2_a15:

	ret	lr
ENDPROC(tegra_init_l2_for_a15)

/*
 * tegra_sleep_cpu_finish(unsigned long v2p)
 *
 * enters suspend in LP2 by turning off the mmu and jumping to
 * tegra?_tear_down_cpu
 */
ENTRY(tegra_sleep_cpu_finish)
	mov	r4, r0
	/* Flush and disable the L1 data cache */
	mov	r0, #TEGRA_FLUSH_CACHE_ALL
	bl	tegra_disable_clean_inv_dcache

	mov	r0, r4
	mov32	r6, tegra_tear_down_cpu
	ldr	r1, [r6]
	add	r1, r1, r0

	mov32	r3, tegra_shut_off_mmu
	add	r3, r3, r0
	mov	r0, r1

	ret	r3
ENDPROC(tegra_sleep_cpu_finish)

/*
 * tegra_shut_off_mmu
 *
 * r0 = physical address to jump to with mmu off
 *
 * called with VA=PA mapping
 * turns off MMU, icache, dcache and branch prediction
 */
	.align	L1_CACHE_SHIFT
	.pushsection	.idmap.text, "ax"
ENTRY(tegra_shut_off_mmu)
	mrc	p15, 0, r3, c1, c0, 0
	movw	r2, #CR_I | CR_Z | CR_C | CR_M
	bic	r3, r3, r2
	dsb
	mcr	p15, 0, r3, c1, c0, 0
	isb
#ifdef CONFIG_CACHE_L2X0
	/* Disable L2 cache */
	check_cpu_part_num 0xc09, r9, r10
	movweq	r2, #:lower16:(TEGRA_ARM_PERIF_BASE + 0x3000)
	movteq	r2, #:upper16:(TEGRA_ARM_PERIF_BASE + 0x3000)
	moveq	r3, #0
	streq	r3, [r2, #L2X0_CTRL]
#endif
	ret	r0
ENDPROC(tegra_shut_off_mmu)
	.popsection

/*
 * tegra_switch_cpu_to_pllp
 *
 * In LP2 the normal cpu clock pllx will be turned off. Switch the CPU to pllp
 */
ENTRY(tegra_switch_cpu_to_pllp)
	/* in LP2 idle (SDRAM active), set the CPU burst policy to PLLP */
	mov32	r5, TEGRA_CLK_RESET_BASE
	mov	r0, #(2 << 28)			@ burst policy = run mode
	orr	r0, r0, #(4 << 4)		@ use PLLP in run mode burst
	str	r0, [r5, #CLK_RESET_CCLK_BURST]
	mov	r0, #0
	str	r0, [r5, #CLK_RESET_CCLK_DIVIDER]
	ret	lr
ENDPROC(tegra_switch_cpu_to_pllp)
#endif
