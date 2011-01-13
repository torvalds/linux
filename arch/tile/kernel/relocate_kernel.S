/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * copy new kernel into place and then call hv_reexec
 *
 */

#include <linux/linkage.h>
#include <arch/chip.h>
#include <asm/page.h>
#include <hv/hypervisor.h>

#define ___hvb	MEM_SV_INTRPT + HV_GLUE_START_CPA

#define ___hv_dispatch(f) (___hvb + (HV_DISPATCH_ENTRY_SIZE * f))

#define ___hv_console_putc ___hv_dispatch(HV_DISPATCH_CONSOLE_PUTC)
#define ___hv_halt         ___hv_dispatch(HV_DISPATCH_HALT)
#define ___hv_reexec       ___hv_dispatch(HV_DISPATCH_REEXEC)
#define ___hv_flush_remote ___hv_dispatch(HV_DISPATCH_FLUSH_REMOTE)

#undef RELOCATE_NEW_KERNEL_VERBOSE

STD_ENTRY(relocate_new_kernel)

	move	r30, r0		/* page list */
	move	r31, r1		/* address of page we are on */
	move	r32, r2		/* start address of new kernel */

	shri	r1, r1, PAGE_SHIFT
	addi	r1, r1, 1
	shli	sp, r1, PAGE_SHIFT
	addi	sp, sp, -8
	/* we now have a stack (whether we need one or not) */

	moveli	r40, lo16(___hv_console_putc)
	auli	r40, r40, ha16(___hv_console_putc)

#ifdef RELOCATE_NEW_KERNEL_VERBOSE
	moveli	r0, 'r'
	jalr	r40

	moveli	r0, '_'
	jalr	r40

	moveli	r0, 'n'
	jalr	r40

	moveli	r0, '_'
	jalr	r40

	moveli	r0, 'k'
	jalr	r40

	moveli	r0, '\n'
	jalr	r40
#endif

	/*
	 * Throughout this code r30 is pointer to the element of page
	 * list we are working on.
	 *
	 * Normally we get to the next element of the page list by
	 * incrementing r30 by four.  The exception is if the element
	 * on the page list is an IND_INDIRECTION in which case we use
	 * the element with the low bits masked off as the new value
	 * of r30.
	 *
	 * To get this started, we need the value passed to us (which
	 * will always be an IND_INDIRECTION) in memory somewhere with
	 * r30 pointing at it.  To do that, we push the value passed
	 * to us on the stack and make r30 point to it.
	 */

	sw	sp, r30
	move	r30, sp
	addi	sp, sp, -8

#if CHIP_HAS_CBOX_HOME_MAP()
	/*
	 * On TILEPro, we need to flush all tiles' caches, since we may
	 * have been doing hash-for-home caching there.  Note that we
	 * must do this _after_ we're completely done modifying any memory
	 * other than our output buffer (which we know is locally cached).
	 * We want the caches to be fully clean when we do the reexec,
	 * because the hypervisor is going to do this flush again at that
	 * point, and we don't want that second flush to overwrite any memory.
	 */
	{
	 move	r0, zero	 /* cache_pa */
	 move	r1, zero
	}
	{
	 auli	r2, zero, ha16(HV_FLUSH_EVICT_L2) /* cache_control */
	 movei	r3, -1		 /* cache_cpumask; -1 means all client tiles */
	}
	{
	 move	r4, zero	 /* tlb_va */
	 move	r5, zero	 /* tlb_length */
	}
	{
	 move	r6, zero	 /* tlb_pgsize */
	 move	r7, zero	 /* tlb_cpumask */
	}
	{
	 move	r8, zero	 /* asids */
	 moveli	r20, lo16(___hv_flush_remote)
	}
	{
	 move	r9, zero	 /* asidcount */
	 auli	r20, r20, ha16(___hv_flush_remote)
	}

	jalr	r20
#endif

	/* r33 is destination pointer, default to zero */

	moveli	r33, 0

.Lloop:	lw	r10, r30

	andi	r9, r10, 0xf	/* low 4 bits tell us what type it is */
	xor	r10, r10, r9	/* r10 is now value with low 4 bits stripped */

	seqi	r0, r9, 0x1	/* IND_DESTINATION */
	bzt	r0, .Ltry2

	move	r33, r10

#ifdef RELOCATE_NEW_KERNEL_VERBOSE
	moveli	r0, 'd'
	jalr	r40
#endif

	addi	r30, r30, 4
	j	.Lloop

.Ltry2:
	seqi	r0, r9, 0x2	/* IND_INDIRECTION */
	bzt	r0, .Ltry4

	move	r30, r10

#ifdef RELOCATE_NEW_KERNEL_VERBOSE
	moveli	r0, 'i'
	jalr	r40
#endif

	j	.Lloop

.Ltry4:
	seqi	r0, r9, 0x4	/* IND_DONE */
	bzt	r0, .Ltry8

	mf

#ifdef RELOCATE_NEW_KERNEL_VERBOSE
	moveli	r0, 'D'
	jalr	r40
	moveli	r0, '\n'
	jalr	r40
#endif

	move	r0, r32
	moveli	r1, 0		/* arg to hv_reexec is 64 bits */

	moveli	r41, lo16(___hv_reexec)
	auli	r41, r41, ha16(___hv_reexec)

	jalr	r41

	/* we should not get here */

	moveli	r0, '?'
	jalr	r40
	moveli	r0, '\n'
	jalr	r40

	j	.Lhalt

.Ltry8:	seqi	r0, r9, 0x8	/* IND_SOURCE */
	bz	r0, .Lerr	/* unknown type */

	/* copy page at r10 to page at r33 */

	move	r11, r33

	moveli	r0, lo16(PAGE_SIZE)
	auli	r0, r0, ha16(PAGE_SIZE)
	add	r33, r33, r0

	/* copy word at r10 to word at r11 until r11 equals r33 */

	/* We know page size must be multiple of 16, so we can unroll
	 * 16 times safely without any edge case checking.
	 *
	 * Issue a flush of the destination every 16 words to avoid
	 * incoherence when starting the new kernel.  (Now this is
	 * just good paranoia because the hv_reexec call will also
	 * take care of this.)
	 */

1:
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0; addi	r11, r11, 4 }
	{ lw	r0, r10; addi	r10, r10, 4 }
	{ sw	r11, r0 }
	{ flush r11    ; addi	r11, r11, 4 }

	seq	r0, r33, r11
	bzt	r0, 1b

#ifdef RELOCATE_NEW_KERNEL_VERBOSE
	moveli	r0, 's'
	jalr	r40
#endif

	addi	r30, r30, 4
	j	.Lloop


.Lerr:	moveli	r0, 'e'
	jalr	r40
	moveli	r0, 'r'
	jalr	r40
	moveli	r0, 'r'
	jalr	r40
	moveli	r0, '\n'
	jalr	r40
.Lhalt:
	moveli	r41, lo16(___hv_halt)
	auli	r41, r41, ha16(___hv_halt)

	jalr	r41
	STD_ENDPROC(relocate_new_kernel)

	.section .rodata,"a"

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long .Lend_relocate_new_kernel - relocate_new_kernel
