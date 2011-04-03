/*
 * linux/arch/unicore32/mm/proc-macros.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * We need constants.h for:
 *  VMA_VM_MM
 *  VMA_VM_FLAGS
 *  VM_EXEC
 */
#include <generated/asm-offsets.h>
#include <asm/thread_info.h>
#include <asm/memory.h>

/*
 * the cache line sizes of the I and D cache are the same
 */
#define CACHE_LINESIZE	32

/*
 * This is the maximum size of an area which will be invalidated
 * using the single invalidate entry instructions.  Anything larger
 * than this, and we go for the whole cache.
 *
 * This value should be chosen such that we choose the cheapest
 * alternative.
 */
#ifdef CONFIG_CPU_UCV2
#define MAX_AREA_SIZE	0x800		/* 64 cache line */
#endif

/*
 * vma_vm_mm - get mm pointer from vma pointer (vma->vm_mm)
 */
	.macro	vma_vm_mm, rd, rn
	ldw	\rd, [\rn+], #VMA_VM_MM
	.endm

/*
 * vma_vm_flags - get vma->vm_flags
 */
	.macro	vma_vm_flags, rd, rn
	ldw	\rd, [\rn+], #VMA_VM_FLAGS
	.endm

	.macro	tsk_mm, rd, rn
	ldw	\rd, [\rn+], #TI_TASK
	ldw	\rd, [\rd+], #TSK_ACTIVE_MM
	.endm

/*
 * act_mm - get current->active_mm
 */
	.macro	act_mm, rd
	andn	\rd, sp, #8128
	andn	\rd, \rd, #63
	ldw	\rd, [\rd+], #TI_TASK
	ldw	\rd, [\rd+], #TSK_ACTIVE_MM
	.endm

/*
 * mmid - get context id from mm pointer (mm->context.id)
 */
	.macro	mmid, rd, rn
	ldw	\rd, [\rn+], #MM_CONTEXT_ID
	.endm

/*
 * mask_asid - mask the ASID from the context ID
 */
	.macro	asid, rd, rn
	and	\rd, \rn, #255
	.endm

	.macro	crval, clear, mmuset, ucset
	.word	\clear
	.word	\mmuset
	.endm

#ifndef CONFIG_CPU_DCACHE_LINE_DISABLE
/*
 * va2pa va, pa, tbl, msk, off, err
 *	This macro is used to translate virtual address to its physical address.
 *
 *	va: virtual address
 *	pa: physical address, result is stored in this register
 *	tbl, msk, off:	temp registers, will be destroyed
 *	err: jump to error label if the physical address not exist
 * NOTE: all regs must be different
 */
	.macro	va2pa, va, pa, tbl, msk, off, err=990f
	movc	\pa, p0.c2, #0
	mov	\off, \va >> #22		@ off <- index of 1st page table
	adr	\tbl, 910f			@ tbl <- table of 1st page table
900:						@ ---- handle 1, 2 page table
	add	\pa, \pa, #PAGE_OFFSET		@ pa <- virt addr of page table
	ldw	\pa, [\pa+], \off << #2		@ pa <- the content of pt
	cand.a	\pa, #4				@ test exist bit
	beq	\err				@ if not exist
	and	\off, \pa, #3			@ off <- the last 2 bits
	add	\tbl, \tbl, \off << #3		@ cmove table pointer
	ldw	\msk, [\tbl+], #0		@ get the mask
	ldw	pc, [\tbl+], #4
930:						@ ---- handle 2nd page table
	and	\pa, \pa, \msk			@ pa <- phys addr of 2nd pt
	mov	\off, \va << #10
	cntlo	\tbl, \msk			@ use tbl as temp reg
	mov	\off, \off >> \tbl
	mov	\off, \off >> #2		@ off <- index of 2nd pt
	adr	\tbl, 920f			@ tbl <- table of 2nd pt
	b	900b
910:						@ 1st level page table
	.word	0xfffff000, 930b		@ second level page table
	.word	0xfffffc00, 930b		@ second level large page table
	.word	0x00000000, \err		@ invalid
	.word	0xffc00000, 980f		@ super page

920:						@ 2nd level page table
	.word	0xfffff000, 980f		@ page
	.word	0xffffc000, 980f		@ middle page
	.word	0xffff0000, 980f		@ large page
	.word	0x00000000, \err		@ invalid
980:
	andn	\tbl, \va, \msk
	and	\pa, \pa, \msk
	or	\pa, \pa, \tbl
990:
	.endm
#endif

	.macro dcacheline_flush, addr, t1, t2
	mov	\t1, \addr << #20
	ldw	\t2, =_stext			@ _stext must ALIGN(4096)
	add	\t2, \t2, \t1 >> #20
	ldw	\t1, [\t2+], #0x0000
	ldw	\t1, [\t2+], #0x1000
	ldw	\t1, [\t2+], #0x2000
	ldw	\t1, [\t2+], #0x3000
	.endm
