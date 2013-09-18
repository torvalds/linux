/*
 * sh7372 lowlevel sleep code for "Core Standby Mode"
 *
 * Copyright (C) 2011 Magnus Damm
 *
 * In "Core Standby Mode" the ARM core is off, but L2 cache is still on
 *
 * Based on mach-omap2/sleep34xx.S
 *
 * (C) Copyright 2007 Texas Instruments
 * Karthik Dasu <karthik-dp@ti.com>
 *
 * (C) Copyright 2004 Texas Instruments, <www.ti.com>
 * Richard Woodruff <r-woodruff2@ti.com>
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
#include <asm/assembler.h>

#if defined(CONFIG_SUSPEND) || defined(CONFIG_CPU_IDLE)
	.align	12
	.text
	.global sh7372_resume_core_standby_sysc
sh7372_resume_core_standby_sysc:
	ldr     pc, 1f

	.globl	sh7372_cpu_resume
sh7372_cpu_resume:
1:	.space	4

#define SPDCR 0xe6180008

	/* A3SM & A4S power down */
	.global	sh7372_do_idle_sysc
sh7372_do_idle_sysc:
	mov	r8, r0 /* sleep mode passed in r0 */

	/*
	 * Clear the SCTLR.C bit to prevent further data cache
	 * allocation. Clearing SCTLR.C would make all the data accesses
	 * strongly ordered and would not hit the cache.
	 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 2)	@ Disable the C bit
	mcr	p15, 0, r0, c1, c0, 0
	isb

	/*
	 * Clean and invalidate data cache again.
	 */
	ldr	r1, kernel_flush
	blx	r1

	/* disable L2 cache in the aux control register */
	mrc     p15, 0, r10, c1, c0, 1
	bic     r10, r10, #2
	mcr     p15, 0, r10, c1, c0, 1
	isb

	/*
	 * The kernel doesn't interwork: v7_flush_dcache_all in particluar will
	 * always return in Thumb state when CONFIG_THUMB2_KERNEL is enabled.
	 * This sequence switches back to ARM.  Note that .align may insert a
	 * nop: bx pc needs to be word-aligned in order to work.
	 */
 THUMB(	.thumb		)
 THUMB(	.align		)
 THUMB(	bx	pc	)
 THUMB(	nop		)
	.arm

	/* Data memory barrier and Data sync barrier */
	dsb
	dmb

	/* SYSC power down */
	ldr     r0, =SPDCR
	str     r8, [r0]
1:
	b      1b

kernel_flush:
	.word v7_flush_dcache_all
#endif
