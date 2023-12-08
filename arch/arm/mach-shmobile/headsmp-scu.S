/* SPDX-License-Identifier: GPL-2.0+
 *
 * Shared SCU setup for mach-shmobile
 *
 * Copyright (C) 2012 Bastian Hecht
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

/*
 * Boot code for secondary CPUs.
 *
 * First we turn on L1 cache coherency for our CPU. Then we jump to
 * secondary_startup that invalidates the cache and hands over control
 * to the common ARM startup code.
 */
ENTRY(shmobile_boot_scu)
					@ r0 = SCU base address
	mrc     p15, 0, r1, c0, c0, 5	@ read MPIDR
	and	r1, r1, #3		@ mask out cpu ID
	lsl	r1, r1, #3		@ we will shift by cpu_id * 8 bits
	ldr	r2, [r0, #8]		@ SCU Power Status Register
	mov	r3, #3
	lsl	r3, r3, r1
	bic	r2, r2, r3		@ Clear bits of our CPU (Run Mode)
	str	r2, [r0, #8]		@ write back

	b	secondary_startup
ENDPROC(shmobile_boot_scu)
