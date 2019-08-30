/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/mm/tlb-ucv2.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/page.h>
#include <asm/tlbflush.h>
#include "proc-macros.S"

/*
 *	__cpu_flush_user_tlb_range(start, end, vma)
 *
 *	Invalidate a range of TLB entries in the specified address space.
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 *	- vma   - vma_struct describing address range
 */
ENTRY(__cpu_flush_user_tlb_range)
#ifndef	CONFIG_CPU_TLB_SINGLE_ENTRY_DISABLE
	mov	r0, r0 >> #PAGE_SHIFT		@ align address
	mov	r0, r0 << #PAGE_SHIFT
	vma_vm_flags r2, r2			@ get vma->vm_flags
1:
	movc	p0.c6, r0, #3
	nop8

	cand.a	r2, #VM_EXEC			@ Executable area ?
	beq	2f

	movc	p0.c6, r0, #5
	nop8
2:
	add	r0, r0, #PAGE_SZ
	csub.a	r0, r1
	beb	1b
#else
	movc	p0.c6, r0, #2
	nop8

	cand.a	r2, #VM_EXEC			@ Executable area ?
	beq	2f

	movc	p0.c6, r0, #4
	nop8
2:
#endif
	mov	pc, lr

/*
 *	__cpu_flush_kern_tlb_range(start,end)
 *
 *	Invalidate a range of kernel TLB entries
 *
 *	- start - start address (may not be aligned)
 *	- end   - end address (exclusive, may not be aligned)
 */
ENTRY(__cpu_flush_kern_tlb_range)
#ifndef	CONFIG_CPU_TLB_SINGLE_ENTRY_DISABLE
	mov	r0, r0 >> #PAGE_SHIFT		@ align address
	mov	r0, r0 << #PAGE_SHIFT
1:
	movc	p0.c6, r0, #3
	nop8

	movc	p0.c6, r0, #5
	nop8

	add	r0, r0, #PAGE_SZ
	csub.a	r0, r1
	beb	1b
#else
	movc	p0.c6, r0, #2
	nop8

	movc	p0.c6, r0, #4
	nop8
#endif
	mov	pc, lr

