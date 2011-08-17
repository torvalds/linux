/*
 * linux/arch/unicore32/kernel/head.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/ptrace.h>
#include <generated/asm-offsets.h>
#include <asm/memory.h>
#include <asm/thread_info.h>
#include <asm/system.h>
#include <asm/pgtable-hwdef.h>

#if (PHYS_OFFSET & 0x003fffff)
#error "PHYS_OFFSET must be at an even 4MiB boundary!"
#endif

#define KERNEL_RAM_VADDR	(PAGE_OFFSET + KERNEL_IMAGE_START)
#define KERNEL_RAM_PADDR	(PHYS_OFFSET + KERNEL_IMAGE_START)

#define KERNEL_PGD_PADDR	(KERNEL_RAM_PADDR - 0x1000)
#define KERNEL_PGD_VADDR	(KERNEL_RAM_VADDR - 0x1000)

#define KERNEL_START		KERNEL_RAM_VADDR
#define KERNEL_END		_end

/*
 * swapper_pg_dir is the virtual address of the initial page table.
 * We place the page tables 4K below KERNEL_RAM_VADDR.  Therefore, we must
 * make sure that KERNEL_RAM_VADDR is correctly set.  Currently, we expect
 * the least significant 16 bits to be 0x8000, but we could probably
 * relax this restriction to KERNEL_RAM_VADDR >= PAGE_OFFSET + 0x1000.
 */
#if (KERNEL_RAM_VADDR & 0xffff) != 0x8000
#error KERNEL_RAM_VADDR must start at 0xXXXX8000
#endif

	.globl	swapper_pg_dir
	.equ	swapper_pg_dir, KERNEL_RAM_VADDR - 0x1000

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * This is normally called from the decompressor code.  The requirements
 * are: MMU = off, D-cache = off, I-cache = dont care
 *
 * This code is mostly position independent, so if you link the kernel at
 * 0xc0008000, you call this at __pa(0xc0008000).
 */
	__HEAD
ENTRY(stext)
	@ set asr
	mov	r0, #PRIV_MODE			@ ensure priv mode
	or	r0, #PSR_R_BIT | PSR_I_BIT	@ disable irqs
	mov.a	asr, r0

	@ process identify
	movc	r0, p0.c0, #0			@ cpuid
	movl	r1, 0xff00ffff			@ mask
	movl	r2, 0x4d000863			@ value
	and	r0, r1, r0
	cxor.a	r0, r2
	bne	__error_p			@ invalid processor id

	/*
	 * Clear the 4K level 1 swapper page table
	 */
	movl	r0, #KERNEL_PGD_PADDR		@ page table address
	mov	r1, #0
	add	r2, r0, #0x1000
101:	stw.w	r1, [r0]+, #4
	stw.w	r1, [r0]+, #4
	stw.w	r1, [r0]+, #4
	stw.w	r1, [r0]+, #4
	cxor.a	r0, r2
	bne	101b

	movl	r4, #KERNEL_PGD_PADDR		@ page table address
	mov	r7, #PMD_TYPE_SECT | PMD_PRESENT	@ page size: section
	or	r7, r7, #PMD_SECT_CACHEABLE		@ cacheable
	or	r7, r7, #PMD_SECT_READ | PMD_SECT_WRITE | PMD_SECT_EXEC

	/*
	 * Create identity mapping for first 4MB of kernel to
	 * cater for the MMU enable.  This identity mapping
	 * will be removed by paging_init().  We use our current program
	 * counter to determine corresponding section base address.
	 */
	mov	r6, pc
	mov	r6, r6 >> #22			@ start of kernel section
	or	r1, r7, r6 << #22		@ flags + kernel base
	stw	r1, [r4+], r6 << #2		@ identity mapping

	/*
	 * Now setup the pagetables for our kernel direct
	 * mapped region.
	 */
	add	r0, r4,  #(KERNEL_START & 0xff000000) >> 20
	stw.w	r1, [r0+], #(KERNEL_START & 0x00c00000) >> 20
	movl	r6, #(KERNEL_END - 1)
	add	r0, r0, #4
	add	r6, r4, r6 >> #20
102:	csub.a	r0, r6
	add	r1, r1, #1 << 22
	bua	103f
	stw.w	r1, [r0]+, #4
	b	102b
103:
	/*
	 * Then map first 4MB of ram in case it contains our boot params.
	 */
	add	r0, r4, #PAGE_OFFSET >> 20
	or	r6, r7, #(PHYS_OFFSET & 0xffc00000)
	stw	r6, [r0]

	ldw	r15, __switch_data		@ address to jump to after

	/*
	 * Initialise TLB, Caches, and MMU state ready to switch the MMU
	 * on.
	 */
	mov	r0, #0
	movc	p0.c5, r0, #28			@ cache invalidate all
	nop8
	movc	p0.c6, r0, #6			@ TLB invalidate all
	nop8

	/*
	 * ..V. .... ..TB IDAM
	 * ..1. .... ..01 1111
	 */
	movl	r0, #0x201f			@ control register setting

	/*
	 * Setup common bits before finally enabling the MMU.  Essentially
	 * this is just loading the page table pointer and domain access
	 * registers.
	 */
	#ifndef CONFIG_ALIGNMENT_TRAP
		andn	r0, r0, #CR_A
	#endif
	#ifdef CONFIG_CPU_DCACHE_DISABLE
		andn	r0, r0, #CR_D
	#endif
	#ifdef CONFIG_CPU_DCACHE_WRITETHROUGH
		andn	r0, r0, #CR_B
	#endif
	#ifdef CONFIG_CPU_ICACHE_DISABLE
		andn	r0, r0, #CR_I
	#endif

	movc	p0.c2, r4, #0			@ set pgd
	b	__turn_mmu_on
ENDPROC(stext)

/*
 * Enable the MMU.  This completely changes the structure of the visible
 * memory space.  You will not be able to trace execution through this.
 *
 *  r0  = cp#0 control register
 *  r15 = *virtual* address to jump to upon completion
 */
	.align	5
__turn_mmu_on:
	mov	r0, r0
	movc	p0.c1, r0, #0			@ write control reg
	nop					@ fetch inst by phys addr
	mov	pc, r15
	nop8					@ fetch inst by phys addr
ENDPROC(__turn_mmu_on)

/*
 * Setup the initial page tables.  We only setup the barest
 * amount which are required to get the kernel running, which
 * generally means mapping in the kernel code.
 *
 * r9  = cpuid
 * r10 = procinfo
 *
 * Returns:
 *  r0, r3, r6, r7 corrupted
 *  r4 = physical page table address
 */
	.ltorg

	.align	2
	.type	__switch_data, %object
__switch_data:
	.long	__mmap_switched
	.long	__bss_start			@ r6
	.long	_end				@ r7
	.long	cr_alignment			@ r8
	.long	init_thread_union + THREAD_START_SP @ sp

/*
 * The following fragment of code is executed with the MMU on in MMU mode,
 * and uses absolute addresses; this is not position independent.
 *
 *  r0  = cp#0 control register
 */
__mmap_switched:
	adr	r3, __switch_data + 4

	ldm.w	(r6, r7, r8), [r3]+
	ldw	sp, [r3]

	mov	fp, #0				@ Clear BSS (and zero fp)
203:	csub.a	r6, r7
	bea	204f
	stw.w	fp, [r6]+,#4
	b	203b
204:
	andn	r1, r0, #CR_A			@ Clear 'A' bit
	stm	(r0, r1), [r8]+			@ Save control register values
	b	start_kernel
ENDPROC(__mmap_switched)

/*
 * Exception handling.  Something went wrong and we can't proceed.  We
 * ought to tell the user, but since we don't have any guarantee that
 * we're even running on the right architecture, we do virtually nothing.
 *
 * If CONFIG_DEBUG_LL is set we try to print out something about the error
 * and hope for the best (useful if bootloader fails to pass a proper
 * machine ID for example).
 */
__error_p:
#ifdef CONFIG_DEBUG_LL
	adr	r0, str_p1
	b.l	printascii
	mov	r0, r9
	b.l	printhex8
	adr	r0, str_p2
	b.l	printascii
901:	nop8
	b	901b
str_p1:	.asciz	"\nError: unrecognized processor variant (0x"
str_p2:	.asciz	").\n"
	.align
#endif
ENDPROC(__error_p)

