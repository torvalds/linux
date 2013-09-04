/*
 * Shared SCU setup for mach-shmobile
 *
 * Copyright (C) 2012 Bastian Hecht
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR /PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

/*
 * Boot code for secondary CPUs.
 *
 * First we turn on L1 cache coherency for our CPU. Then we jump to
 * shmobile_invalidate_start that invalidates the cache and hands over control
 * to the common ARM startup code.
 */
ENTRY(shmobile_boot_scu)
					@ r0 = SCU base address
	mrc     p15, 0, r1, c0, c0, 5	@ read MIPDR
	and	r1, r1, #3		@ mask out cpu ID
	lsl	r1, r1, #3		@ we will shift by cpu_id * 8 bits
	ldr	r2, [r0, #8]		@ SCU Power Status Register
	mov	r3, #3
	bic	r2, r2, r3, lsl r1	@ Clear bits of our CPU (Run Mode)
	str	r2, [r0, #8]		@ write back

	b	shmobile_invalidate_start
ENDPROC(shmobile_boot_scu)

	.text
	.globl	shmobile_scu_base
shmobile_scu_base:
	.space	4
