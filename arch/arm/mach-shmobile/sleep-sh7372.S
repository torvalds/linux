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
#include <asm/assembler.h>

#define SMFRAM 0xe6a70000

	.align
kernel_flush:
	.word	v7_flush_dcache_all

	.align	3
ENTRY(sh7372_cpu_suspend)
	stmfd	sp!, {r0-r12, lr}	@ save registers on stack

	ldr	r8, =SMFRAM

	mov	r4, sp			@ Store sp
	mrs	r5, spsr		@ Store spsr
	mov	r6, lr			@ Store lr
	stmia	r8!, {r4-r6}

	mrc	p15, 0, r4, c1, c0, 2	@ Coprocessor access control register
	mrc	p15, 0, r5, c2, c0, 0	@ TTBR0
	mrc	p15, 0, r6, c2, c0, 1	@ TTBR1
	mrc	p15, 0, r7, c2, c0, 2	@ TTBCR
	stmia	r8!, {r4-r7}

	mrc	p15, 0, r4, c3, c0, 0	@ Domain access Control Register
	mrc	p15, 0, r5, c10, c2, 0	@ PRRR
	mrc	p15, 0, r6, c10, c2, 1	@ NMRR
	stmia	r8!,{r4-r6}

	mrc	p15, 0, r4, c13, c0, 1	@ Context ID
	mrc	p15, 0, r5, c13, c0, 2	@ User r/w thread and process ID
	mrc	p15, 0, r6, c12, c0, 0	@ Secure or NS vector base address
	mrs	r7, cpsr		@ Store current cpsr
	stmia	r8!, {r4-r7}

	mrc	p15, 0, r4, c1, c0, 0	@ save control register
	stmia	r8!, {r4}

	/*
	 * jump out to kernel flush routine
	 *  - reuse that code is better
	 *  - it executes in a cached space so is faster than refetch per-block
	 *  - should be faster and will change with kernel
	 *  - 'might' have to copy address, load and jump to it
	 * Flush all data from the L1 data cache before disabling
	 * SCTLR.C bit.
	 */
	ldr	r1, kernel_flush
	mov	lr, pc
	bx	r1

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
	 * Invalidate L1 data cache. Even though only invalidate is
	 * necessary exported flush API is used here. Doing clean
	 * on already clean cache would be almost NOP.
	 */
	ldr	r1, kernel_flush
	blx	r1
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

/*
 * ===================================
 * == WFI instruction => Enter idle ==
 * ===================================
 */
	wfi				@ wait for interrupt

/*
 * ===================================
 * == Resume path for non-OFF modes ==
 * ===================================
 */
	mrc	p15, 0, r0, c1, c0, 0
	tst	r0, #(1 << 2)		@ Check C bit enabled?
	orreq	r0, r0, #(1 << 2)	@ Enable the C bit if cleared
	mcreq	p15, 0, r0, c1, c0, 0
	isb

/*
 * ===================================
 * == Exit point from non-OFF modes ==
 * ===================================
 */
	ldmfd	sp!, {r0-r12, pc}	@ restore regs and return

	.pool

	.align	12
	.text
	.global	sh7372_cpu_resume
sh7372_cpu_resume:

	mov	r1, #0
	/*
	 * Invalidate all instruction caches to PoU
	 * and flush branch target cache
	 */
	mcr	p15, 0, r1, c7, c5, 0

	ldr	r3, =SMFRAM

	ldmia	r3!, {r4-r6}
	mov	sp, r4			@ Restore sp
	msr	spsr_cxsf, r5		@ Restore spsr
	mov	lr, r6			@ Restore lr

	ldmia	r3!, {r4-r7}
	mcr	p15, 0, r4, c1, c0, 2	@ Coprocessor access Control Register
	mcr	p15, 0, r5, c2, c0, 0	@ TTBR0
	mcr	p15, 0, r6, c2, c0, 1	@ TTBR1
	mcr	p15, 0, r7, c2, c0, 2	@ TTBCR

	ldmia	r3!,{r4-r6}
	mcr	p15, 0, r4, c3, c0, 0	@ Domain access Control Register
	mcr	p15, 0, r5, c10, c2, 0	@ PRRR
	mcr	p15, 0, r6, c10, c2, 1	@ NMRR

	ldmia	r3!,{r4-r7}
	mcr	p15, 0, r4, c13, c0, 1	@ Context ID
	mcr	p15, 0, r5, c13, c0, 2	@ User r/w thread and process ID
	mrc	p15, 0, r6, c12, c0, 0	@ Secure or NS vector base address
	msr	cpsr, r7		@ store cpsr

	/* Starting to enable MMU here */
	mrc	p15, 0, r7, c2, c0, 2 	@ Read TTBRControl
	/* Extract N (0:2) bits and decide whether to use TTBR0 or TTBR1 */
	and	r7, #0x7
	cmp	r7, #0x0
	beq	usettbr0
ttbr_error:
	/*
	 * More work needs to be done to support N[0:2] value other than 0
	 * So looping here so that the error can be detected
	 */
	b	ttbr_error

	.align
cache_pred_disable_mask:
	.word	0xFFFFE7FB
ttbrbit_mask:
	.word	0xFFFFC000
table_index_mask:
	.word	0xFFF00000
table_entry:
	.word	0x00000C02
usettbr0:

	mrc	p15, 0, r2, c2, c0, 0
	ldr	r5, ttbrbit_mask
	and	r2, r5
	mov	r4, pc
	ldr	r5, table_index_mask
	and	r4, r5			@ r4 = 31 to 20 bits of pc
	/* Extract the value to be written to table entry */
	ldr	r6, table_entry
	/* r6 has the value to be written to table entry */
	add	r6, r6, r4
	/* Getting the address of table entry to modify */
	lsr	r4, #18
	/* r2 has the location which needs to be modified */
	add	r2, r4
	ldr	r4, [r2]
	str	r6, [r2] /* modify the table entry */

	mov	r7, r6
	mov	r5, r2
	mov	r6, r4
	/* r5 = original page table address */
	/* r6 = original page table data */

	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 4	@ Flush prefetch buffer
	mcr	p15, 0, r0, c7, c5, 6	@ Invalidate branch predictor array
	mcr	p15, 0, r0, c8, c5, 0	@ Invalidate instruction TLB
	mcr	p15, 0, r0, c8, c6, 0	@ Invalidate data TLB

	/*
	 * Restore control register. This enables the MMU.
	 * The caches and prediction are not enabled here, they
	 * will be enabled after restoring the MMU table entry.
	 */
	ldmia	r3!, {r4}
	stmia	r3!, {r5} /* save original page table address */
	stmia	r3!, {r6} /* save original page table data */
	stmia	r3!, {r7} /* save modified page table data */

	ldr	r2, cache_pred_disable_mask
	and	r4, r2
	mcr	p15, 0, r4, c1, c0, 0
	dsb
	isb

	ldr     r0, =restoremmu_on
	bx      r0

/*
 * ==============================
 * == Exit point from OFF mode ==
 * ==============================
 */
restoremmu_on:

	ldmfd	sp!, {r0-r12, pc}	@ restore regs and return
