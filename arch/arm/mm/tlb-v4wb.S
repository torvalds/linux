/*
 *  linux/arch/arm/mm/tlbv4wb.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ARM architecture version 4 TLB handling functions.
 *  These assume a split I/D TLBs w/o I TLB entry, with a write buffer.
 *
 *  Processors: SA110 SA1100 SA1110
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/constants.h>
#include <asm/tlbflush.h>
#include "proc-macros.S"

	.align	5
/*
 *	v4wb_flush_user_tlb_range(start, end, mm)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - range start address
 *	- end   - range end address
 *	- mm    - mm_struct describing address space
 */
	.align	5
ENTRY(v4wb_flush_user_tlb_range)
	vma_vm_mm ip, r2
	act_mm	r3				@ get current->active_mm
	eors	r3, ip, r3				@ == mm ?
	movne	pc, lr				@ no, we dont do anything
	vma_vm_flags r2, r2
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	tst	r2, #VM_EXEC
	mcrne	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 *	v4_flush_kern_tlb_range(start, end)
 *
 *	Invalidate a range of TLB entries in the specified kernel
 *	address range.
 *
 *	- start - virtual address (may not be aligned)
 *	- end   - virtual address (may not be aligned)
 */
ENTRY(v4wb_flush_kern_tlb_range)
	mov	r3, #0
	mcr	p15, 0, r3, c7, c10, 4		@ drain WB
	bic	r0, r0, #0x0ff
	bic	r0, r0, #0xf00
	mcr	p15, 0, r3, c8, c5, 0		@ invalidate I TLB
1:	mcr	p15, 0, r0, c8, c6, 1		@ invalidate D TLB entry
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

	__INITDATA

	.type	v4wb_tlb_fns, #object
ENTRY(v4wb_tlb_fns)
	.long	v4wb_flush_user_tlb_range
	.long	v4wb_flush_kern_tlb_range
	.long	v4wb_tlb_flags
	.size	v4wb_tlb_fns, . - v4wb_tlb_fns
