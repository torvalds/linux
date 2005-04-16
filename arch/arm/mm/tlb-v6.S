/*
 *  linux/arch/arm/mm/tlb-v6.S
 *
 *  Copyright (C) 1997-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ARM architecture version 6 TLB handling functions.
 *  These assume a split I/D TLB.
 */
#include <linux/linkage.h>
#include <asm/constants.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#include "proc-macros.S"

#define HARVARD_TLB

/*
 *	v6wbi_flush_user_tlb_range(start, end, vma)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 *	- vma   - vma_struct describing address range
 *
 *	It is assumed that:
 *	- the "Invalidate single entry" instruction will invalidate
 *	  both the I and the D TLBs on Harvard-style TLBs
 */
ENTRY(v6wbi_flush_user_tlb_range)
	vma_vm_mm r3, r2			@ get vma->vm_mm
	mov	ip, #0
	mmid	r3, r3				@ get vm_mm->context.id
	mcr	p15, 0, ip, c7, c10, 4		@ drain write buffer
	mov	r0, r0, lsr #PAGE_SHIFT		@ align address
	mov	r1, r1, lsr #PAGE_SHIFT
	asid	r3, r3				@ mask ASID
	orr	r0, r3, r0, lsl #PAGE_SHIFT	@ Create initial MVA
	mov	r1, r1, lsl #PAGE_SHIFT
	vma_vm_flags r2, r2			@ get vma->vm_flags
1:
#ifdef HARVARD_TLB
	mcr	p15, 0, r0, c8, c6, 1		@ TLB invalidate D MVA (was 1)
	tst	r2, #VM_EXEC			@ Executable area ?
	mcrne	p15, 0, r0, c8, c5, 1		@ TLB invalidate I MVA (was 1)
#else
	mcr	p15, 0, r0, c8, c7, 1		@ TLB invalidate MVA (was 1)
#endif
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

/*
 *	v6wbi_flush_kern_tlb_range(start,end)
 *
 *	Invalidate a range of kernel TLB entries
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 */
ENTRY(v6wbi_flush_kern_tlb_range)
	mov	r2, #0
	mcr	p15, 0, r2, c7, c10, 4		@ drain write buffer
	mov	r0, r0, lsr #PAGE_SHIFT		@ align address
	mov	r1, r1, lsr #PAGE_SHIFT
	mov	r0, r0, lsl #PAGE_SHIFT
	mov	r1, r1, lsl #PAGE_SHIFT
1:
#ifdef HARVARD_TLB
	mcr	p15, 0, r0, c8, c6, 1		@ TLB invalidate D MVA
	mcr	p15, 0, r0, c8, c5, 1		@ TLB invalidate I MVA
#else
	mcr	p15, 0, r0, c8, c7, 1		@ TLB invalidate MVA
#endif
	add	r0, r0, #PAGE_SZ
	cmp	r0, r1
	blo	1b
	mov	pc, lr

	.section ".text.init", #alloc, #execinstr

	.type	v6wbi_tlb_fns, #object
ENTRY(v6wbi_tlb_fns)
	.long	v6wbi_flush_user_tlb_range
	.long	v6wbi_flush_kern_tlb_range
	.long	v6wbi_tlb_flags
	.size	v6wbi_tlb_fns, . - v6wbi_tlb_fns
