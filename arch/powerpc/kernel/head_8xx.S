/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *  MPC8xx modifications by Dan Malek
 *    Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This file contains low-level support and setup for PowerPC 8xx
 *  embedded processors, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/init.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/export.h>
#include <asm/code-patching-asm.h>

#include "head_32.h"

#if CONFIG_TASK_SIZE <= 0x80000000 && CONFIG_PAGE_OFFSET >= 0x80000000
/* By simply checking Address >= 0x80000000, we know if its a kernel address */
#define SIMPLE_KERNEL_ADDRESS		1
#endif

/*
 * We need an ITLB miss handler for kernel addresses if:
 * - Either we have modules
 * - Or we have not pinned the first 8M
 */
#if defined(CONFIG_MODULES) || !defined(CONFIG_PIN_TLB_TEXT) || \
    defined(CONFIG_DEBUG_PAGEALLOC)
#define ITLB_MISS_KERNEL	1
#endif

/*
 * Value for the bits that have fixed value in RPN entries.
 * Also used for tagging DAR for DTLBerror.
 */
#define RPN_PATTERN	0x00f0

#define PAGE_SHIFT_512K		19
#define PAGE_SHIFT_8M		23

	__HEAD
_ENTRY(_stext);
_ENTRY(_start);

/* MPC8xx
 * This port was done on an MBX board with an 860.  Right now I only
 * support an ELF compressed (zImage) boot from EPPC-Bug because the
 * code there loads up some registers before calling us:
 *   r3: ptr to board info data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string
 *   r7: End of command line string
 *
 * I decided to use conditional compilation instead of checking PVR and
 * adding more processor specific branches around code I don't need.
 * Since this is an embedded processor, I also appreciate any memory
 * savings I can get.
 *
 * The MPC8xx does not have any BATs, but it supports large page sizes.
 * We first initialize the MMU to support 8M byte pages, then load one
 * entry into each of the instruction and data TLBs to map the first
 * 8M 1:1.  I also mapped an additional I/O space 1:1 so we can get to
 * the "internal" processor registers before MMU_init is called.
 *
 *	-- Dan
 */
	.globl	__start
__start:
	mr	r31,r3			/* save device tree ptr */

	/* We have to turn on the MMU right away so we get cache modes
	 * set correctly.
	 */
	bl	initial_mmu

/* We now have the lower 8 Meg mapped into TLB entries, and the caches
 * ready to work.
 */

turn_on_mmu:
	mfmsr	r0
	ori	r0,r0,MSR_DR|MSR_IR
	mtspr	SPRN_SRR1,r0
	lis	r0,start_here@h
	ori	r0,r0,start_here@l
	mtspr	SPRN_SRR0,r0
	rfi				/* enables MMU */


#ifdef CONFIG_PERF_EVENTS
	.align	4

	.globl	itlb_miss_counter
itlb_miss_counter:
	.space	4

	.globl	dtlb_miss_counter
dtlb_miss_counter:
	.space	4

	.globl	instruction_counter
instruction_counter:
	.space	4
#endif

/* System reset */
	EXCEPTION(0x100, Reset, system_reset_exception, EXC_XFER_STD)

/* Machine check */
	. = 0x200
MachineCheck:
	EXCEPTION_PROLOG
	mfspr r4,SPRN_DAR
	stw r4,_DAR(r11)
	li r5,RPN_PATTERN
	mtspr SPRN_DAR,r5	/* Tag DAR, to be used in DTLB Error */
	mfspr r5,SPRN_DSISR
	stw r5,_DSISR(r11)
	addi r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_STD(0x200, machine_check_exception)

/* Data access exception.
 * This is "never generated" by the MPC8xx.
 */
	. = 0x300
DataAccess:

/* Instruction access exception.
 * This is "never generated" by the MPC8xx.
 */
	. = 0x400
InstructionAccess:

/* External interrupt */
	EXCEPTION(0x500, HardwareInterrupt, do_IRQ, EXC_XFER_LITE)

/* Alignment exception */
	. = 0x600
Alignment:
	EXCEPTION_PROLOG
	mfspr	r4,SPRN_DAR
	stw	r4,_DAR(r11)
	li	r5,RPN_PATTERN
	mtspr	SPRN_DAR,r5	/* Tag DAR, to be used in DTLB Error */
	mfspr	r5,SPRN_DSISR
	stw	r5,_DSISR(r11)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	EXC_XFER_STD(0x600, alignment_exception)

/* Program check exception */
	EXCEPTION(0x700, ProgramCheck, program_check_exception, EXC_XFER_STD)

/* No FPU on MPC8xx.  This exception is not supposed to happen.
*/
	EXCEPTION(0x800, FPUnavailable, unknown_exception, EXC_XFER_STD)

/* Decrementer */
	EXCEPTION(0x900, Decrementer, timer_interrupt, EXC_XFER_LITE)

	EXCEPTION(0xa00, Trap_0a, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0xb00, Trap_0b, unknown_exception, EXC_XFER_STD)

/* System call */
	. = 0xc00
SystemCall:
	SYSCALL_ENTRY	0xc00

/* Single step - not used on 601 */
	EXCEPTION(0xd00, SingleStep, single_step_exception, EXC_XFER_STD)
	EXCEPTION(0xe00, Trap_0e, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0xf00, Trap_0f, unknown_exception, EXC_XFER_STD)

/* On the MPC8xx, this is a software emulation interrupt.  It occurs
 * for all unimplemented and illegal instructions.
 */
	EXCEPTION(0x1000, SoftEmu, program_check_exception, EXC_XFER_STD)

/* Called from DataStoreTLBMiss when perf TLB misses events are activated */
#ifdef CONFIG_PERF_EVENTS
	patch_site	0f, patch__dtlbmiss_perf
0:	lwz	r10, (dtlb_miss_counter - PAGE_OFFSET)@l(0)
	addi	r10, r10, 1
	stw	r10, (dtlb_miss_counter - PAGE_OFFSET)@l(0)
	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi
#endif

	. = 0x1100
/*
 * For the MPC8xx, this is a software tablewalk to load the instruction
 * TLB.  The task switch loads the M_TWB register with the pointer to the first
 * level table.
 * If we discover there is no second level table (value is zero) or if there
 * is an invalid pte, we load that into the TLB, which causes another fault
 * into the TLB Error interrupt where we can handle such problems.
 * We have to use the MD_xxx registers for the tablewalk because the
 * equivalent MI_xxx registers only perform the attribute functions.
 */

#ifdef CONFIG_8xx_CPU15
#define INVALIDATE_ADJACENT_PAGES_CPU15(addr)	\
	addi	addr, addr, PAGE_SIZE;	\
	tlbie	addr;			\
	addi	addr, addr, -(PAGE_SIZE << 1);	\
	tlbie	addr;			\
	addi	addr, addr, PAGE_SIZE
#else
#define INVALIDATE_ADJACENT_PAGES_CPU15(addr)
#endif

InstructionTLBMiss:
	mtspr	SPRN_SPRG_SCRATCH0, r10
#if defined(ITLB_MISS_KERNEL) || defined(CONFIG_SWAP)
	mtspr	SPRN_SPRG_SCRATCH1, r11
#endif

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	mfspr	r10, SPRN_SRR0	/* Get effective address of fault */
	INVALIDATE_ADJACENT_PAGES_CPU15(r10)
	mtspr	SPRN_MD_EPN, r10
	/* Only modules will cause ITLB Misses as we always
	 * pin the first 8MB of kernel memory */
#ifdef ITLB_MISS_KERNEL
	mfcr	r11
#if defined(SIMPLE_KERNEL_ADDRESS) && defined(CONFIG_PIN_TLB_TEXT)
	cmpi	cr0, r10, 0	/* Address >= 0x80000000 */
#else
	rlwinm	r10, r10, 16, 0xfff8
	cmpli	cr0, r10, PAGE_OFFSET@h
#ifndef CONFIG_PIN_TLB_TEXT
	/* It is assumed that kernel code fits into the first 32M */
0:	cmpli	cr7, r10, (PAGE_OFFSET + 0x2000000)@h
	patch_site	0b, patch__itlbmiss_linmem_top
#endif
#endif
#endif
	mfspr	r10, SPRN_M_TWB	/* Get level 1 table */
#ifdef ITLB_MISS_KERNEL
#if defined(SIMPLE_KERNEL_ADDRESS) && defined(CONFIG_PIN_TLB_TEXT)
	bge+	3f
#else
	blt+	3f
#endif
#ifndef CONFIG_PIN_TLB_TEXT
	blt	cr7, ITLBMissLinear
#endif
	rlwinm	r10, r10, 0, 20, 31
	oris	r10, r10, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
#endif
	lwz	r10, (swapper_pg_dir-PAGE_OFFSET)@l(r10)	/* Get level 1 entry */
	mtspr	SPRN_MI_TWC, r10	/* Set segment attributes */

	mtspr	SPRN_MD_TWC, r10
	mfspr	r10, SPRN_MD_TWC
	lwz	r10, 0(r10)	/* Get the pte */
#ifdef ITLB_MISS_KERNEL
	mtcr	r11
#endif
#ifdef CONFIG_SWAP
	rlwinm	r11, r10, 32-5, _PAGE_PRESENT
	and	r11, r11, r10
	rlwimi	r10, r11, 0, _PAGE_PRESENT
#endif
	/* The Linux PTE won't go exactly into the MMU TLB.
	 * Software indicator bits 20 and 23 must be clear.
	 * Software indicator bits 22, 24, 25, 26, and 27 must be
	 * set.  All other Linux PTE bits control the behavior
	 * of the MMU.
	 */
	rlwimi	r10, r10, 0, 0x0f00	/* Clear bits 20-23 */
	rlwimi	r10, r10, 4, 0x0400	/* Copy _PAGE_EXEC into bit 21 */
	ori	r10, r10, RPN_PATTERN | 0x200 /* Set 22 and 24-27 */
	mtspr	SPRN_MI_RPN, r10	/* Update TLB entry */

	/* Restore registers */
0:	mfspr	r10, SPRN_SPRG_SCRATCH0
#if defined(ITLB_MISS_KERNEL) || defined(CONFIG_SWAP)
	mfspr	r11, SPRN_SPRG_SCRATCH1
#endif
	rfi
	patch_site	0b, patch__itlbmiss_exit_1

#ifdef CONFIG_PERF_EVENTS
	patch_site	0f, patch__itlbmiss_perf
0:	lwz	r10, (itlb_miss_counter - PAGE_OFFSET)@l(0)
	addi	r10, r10, 1
	stw	r10, (itlb_miss_counter - PAGE_OFFSET)@l(0)
	mfspr	r10, SPRN_SPRG_SCRATCH0
#if defined(ITLB_MISS_KERNEL) || defined(CONFIG_SWAP)
	mfspr	r11, SPRN_SPRG_SCRATCH1
#endif
	rfi
#endif

#ifndef CONFIG_PIN_TLB_TEXT
ITLBMissLinear:
	mtcr	r11
#if defined(CONFIG_STRICT_KERNEL_RWX) && CONFIG_ETEXT_SHIFT < 23
	patch_site	0f, patch__itlbmiss_linmem_top8

	mfspr	r10, SPRN_SRR0
0:	subis	r11, r10, (PAGE_OFFSET - 0x80000000)@ha
	rlwinm	r11, r11, 4, MI_PS8MEG ^ MI_PS512K
	ori	r11, r11, MI_PS512K | MI_SVALID
	rlwinm	r10, r10, 0, 0x0ff80000	/* 8xx supports max 256Mb RAM */
#else
	/* Set 8M byte page and mark it valid */
	li	r11, MI_PS8MEG | MI_SVALID
	rlwinm	r10, r10, 20, 0x0f800000	/* 8xx supports max 256Mb RAM */
#endif
	mtspr	SPRN_MI_TWC, r11
	ori	r10, r10, 0xf0 | MI_SPS16K | _PAGE_SH | _PAGE_DIRTY | \
			  _PAGE_PRESENT
	mtspr	SPRN_MI_RPN, r10	/* Update TLB entry */

0:	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi
	patch_site	0b, patch__itlbmiss_exit_2
#endif

	. = 0x1200
DataStoreTLBMiss:
	mtspr	SPRN_SPRG_SCRATCH0, r10
	mtspr	SPRN_SPRG_SCRATCH1, r11
	mfcr	r11

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	mfspr	r10, SPRN_MD_EPN
	rlwinm	r10, r10, 16, 0xfff8
	cmpli	cr0, r10, PAGE_OFFSET@h
#ifndef CONFIG_PIN_TLB_IMMR
	cmpli	cr6, r10, VIRT_IMMR_BASE@h
#endif
0:	cmpli	cr7, r10, (PAGE_OFFSET + 0x2000000)@h
	patch_site	0b, patch__dtlbmiss_linmem_top

	mfspr	r10, SPRN_M_TWB	/* Get level 1 table */
	blt+	3f
#ifndef CONFIG_PIN_TLB_IMMR
0:	beq-	cr6, DTLBMissIMMR
	patch_site	0b, patch__dtlbmiss_immr_jmp
#endif
	blt	cr7, DTLBMissLinear
	rlwinm	r10, r10, 0, 20, 31
	oris	r10, r10, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
	mtcr	r11
	lwz	r11, (swapper_pg_dir-PAGE_OFFSET)@l(r10)	/* Get level 1 entry */

	mtspr	SPRN_MD_TWC, r11
	mfspr	r10, SPRN_MD_TWC
	lwz	r10, 0(r10)	/* Get the pte */

	/* Insert the Guarded flag into the TWC from the Linux PTE.
	 * It is bit 27 of both the Linux PTE and the TWC (at least
	 * I got that right :-).  It will be better when we can put
	 * this into the Linux pgd/pmd and load it in the operation
	 * above.
	 */
	rlwimi	r11, r10, 0, _PAGE_GUARDED
	mtspr	SPRN_MD_TWC, r11

	/* Both _PAGE_ACCESSED and _PAGE_PRESENT has to be set.
	 * We also need to know if the insn is a load/store, so:
	 * Clear _PAGE_PRESENT and load that which will
	 * trap into DTLB Error with store bit set accordinly.
	 */
	/* PRESENT=0x1, ACCESSED=0x20
	 * r11 = ((r10 & PRESENT) & ((r10 & ACCESSED) >> 5));
	 * r10 = (r10 & ~PRESENT) | r11;
	 */
#ifdef CONFIG_SWAP
	rlwinm	r11, r10, 32-5, _PAGE_PRESENT
	and	r11, r11, r10
	rlwimi	r10, r11, 0, _PAGE_PRESENT
#endif
	/* The Linux PTE won't go exactly into the MMU TLB.
	 * Software indicator bits 24, 25, 26, and 27 must be
	 * set.  All other Linux PTE bits control the behavior
	 * of the MMU.
	 */
	li	r11, RPN_PATTERN
	rlwimi	r10, r11, 0, 24, 27	/* Set 24-27 */
	mtspr	SPRN_MD_RPN, r10	/* Update TLB entry */

	/* Restore registers */
	mtspr	SPRN_DAR, r11	/* Tag DAR */

0:	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi
	patch_site	0b, patch__dtlbmiss_exit_1

DTLBMissIMMR:
	mtcr	r11
	/* Set 512k byte guarded page and mark it valid */
	li	r10, MD_PS512K | MD_GUARDED | MD_SVALID
	mtspr	SPRN_MD_TWC, r10
	mfspr	r10, SPRN_IMMR			/* Get current IMMR */
	rlwinm	r10, r10, 0, 0xfff80000		/* Get 512 kbytes boundary */
	ori	r10, r10, 0xf0 | MD_SPS16K | _PAGE_SH | _PAGE_DIRTY | \
			  _PAGE_PRESENT | _PAGE_NO_CACHE
	mtspr	SPRN_MD_RPN, r10	/* Update TLB entry */

	li	r11, RPN_PATTERN
	mtspr	SPRN_DAR, r11	/* Tag DAR */

0:	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi
	patch_site	0b, patch__dtlbmiss_exit_2

DTLBMissLinear:
	mtcr	r11
	rlwinm	r10, r10, 20, 0x0f800000	/* 8xx supports max 256Mb RAM */
#if defined(CONFIG_STRICT_KERNEL_RWX) && CONFIG_DATA_SHIFT < 23
	patch_site	0f, patch__dtlbmiss_romem_top8

0:	subis	r11, r10, (PAGE_OFFSET - 0x80000000)@ha
	rlwinm	r11, r11, 0, 0xff800000
	neg	r10, r11
	or	r11, r11, r10
	rlwinm	r11, r11, 4, MI_PS8MEG ^ MI_PS512K
	ori	r11, r11, MI_PS512K | MI_SVALID
	mfspr	r10, SPRN_MD_EPN
	rlwinm	r10, r10, 0, 0x0ff80000	/* 8xx supports max 256Mb RAM */
#else
	/* Set 8M byte page and mark it valid */
	li	r11, MD_PS8MEG | MD_SVALID
#endif
	mtspr	SPRN_MD_TWC, r11
#ifdef CONFIG_STRICT_KERNEL_RWX
	patch_site	0f, patch__dtlbmiss_romem_top

0:	subis	r11, r10, 0
	rlwimi	r10, r11, 11, _PAGE_RO
#endif
	ori	r10, r10, 0xf0 | MD_SPS16K | _PAGE_SH | _PAGE_DIRTY | \
			  _PAGE_PRESENT
	mtspr	SPRN_MD_RPN, r10	/* Update TLB entry */

	li	r11, RPN_PATTERN
	mtspr	SPRN_DAR, r11	/* Tag DAR */

0:	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi
	patch_site	0b, patch__dtlbmiss_exit_3

/* This is an instruction TLB error on the MPC8xx.  This could be due
 * to many reasons, such as executing guarded memory or illegal instruction
 * addresses.  There is nothing to do but handle a big time error fault.
 */
	. = 0x1300
InstructionTLBError:
	EXCEPTION_PROLOG
	mr	r4,r12
	andis.	r5,r9,DSISR_SRR1_MATCH_32S@h /* Filter relevant SRR1 bits */
	andis.	r10,r9,SRR1_ISI_NOPT@h
	beq+	.Litlbie
	tlbie	r4
	/* 0x400 is InstructionAccess exception, needed by bad_page_fault() */
.Litlbie:
	EXC_XFER_LITE(0x400, handle_page_fault)

/* This is the data TLB error on the MPC8xx.  This could be due to
 * many reasons, including a dirty update to a pte.  We bail out to
 * a higher level function that can handle it.
 */
	. = 0x1400
DataTLBError:
	mtspr	SPRN_SPRG_SCRATCH0, r10
	mtspr	SPRN_SPRG_SCRATCH1, r11
	mfcr	r10

	mfspr	r11, SPRN_DAR
	cmpwi	cr0, r11, RPN_PATTERN
	beq-	FixupDAR	/* must be a buggy dcbX, icbi insn. */
DARFixed:/* Return from dcbx instruction bug workaround */
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2
	mfspr	r5,SPRN_DSISR
	stw	r5,_DSISR(r11)
	mfspr	r4,SPRN_DAR
	andis.	r10,r5,DSISR_NOHPTE@h
	beq+	.Ldtlbie
	tlbie	r4
.Ldtlbie:
	li	r10,RPN_PATTERN
	mtspr	SPRN_DAR,r10	/* Tag DAR, to be used in DTLB Error */
	/* 0x300 is DataAccess exception, needed by bad_page_fault() */
	EXC_XFER_LITE(0x300, handle_page_fault)

	EXCEPTION(0x1500, Trap_15, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1600, Trap_16, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1700, Trap_17, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1800, Trap_18, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1900, Trap_19, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1a00, Trap_1a, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1b00, Trap_1b, unknown_exception, EXC_XFER_STD)

/* On the MPC8xx, these next four traps are used for development
 * support of breakpoints and such.  Someday I will get around to
 * using them.
 */
	. = 0x1c00
DataBreakpoint:
	mtspr	SPRN_SPRG_SCRATCH0, r10
	mtspr	SPRN_SPRG_SCRATCH1, r11
	mfcr	r10
	mfspr	r11, SPRN_SRR0
	cmplwi	cr0, r11, (.Ldtlbie - PAGE_OFFSET)@l
	cmplwi	cr7, r11, (.Litlbie - PAGE_OFFSET)@l
	beq-	cr0, 11f
	beq-	cr7, 11f
	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2
	addi	r3,r1,STACK_FRAME_OVERHEAD
	mfspr	r4,SPRN_BAR
	stw	r4,_DAR(r11)
	mfspr	r5,SPRN_DSISR
	EXC_XFER_STD(0x1c00, do_break)
11:
	mtcr	r10
	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi

#ifdef CONFIG_PERF_EVENTS
	. = 0x1d00
InstructionBreakpoint:
	mtspr	SPRN_SPRG_SCRATCH0, r10
	lwz	r10, (instruction_counter - PAGE_OFFSET)@l(0)
	addi	r10, r10, -1
	stw	r10, (instruction_counter - PAGE_OFFSET)@l(0)
	lis	r10, 0xffff
	ori	r10, r10, 0x01
	mtspr	SPRN_COUNTA, r10
	mfspr	r10, SPRN_SPRG_SCRATCH0
	rfi
#else
	EXCEPTION(0x1d00, Trap_1d, unknown_exception, EXC_XFER_STD)
#endif
	EXCEPTION(0x1e00, Trap_1e, unknown_exception, EXC_XFER_STD)
	EXCEPTION(0x1f00, Trap_1f, unknown_exception, EXC_XFER_STD)

	. = 0x2000

/* This is the procedure to calculate the data EA for buggy dcbx,dcbi instructions
 * by decoding the registers used by the dcbx instruction and adding them.
 * DAR is set to the calculated address.
 */
 /* define if you don't want to use self modifying code */
#define NO_SELF_MODIFYING_CODE
FixupDAR:/* Entry point for dcbx workaround. */
	mtspr	SPRN_M_TW, r10
	/* fetch instruction from memory. */
	mfspr	r10, SPRN_SRR0
	mtspr	SPRN_MD_EPN, r10
	rlwinm	r11, r10, 16, 0xfff8
	cmpli	cr0, r11, PAGE_OFFSET@h
	mfspr	r11, SPRN_M_TWB	/* Get level 1 table */
	blt+	3f
	rlwinm	r11, r10, 16, 0xfff8

0:	cmpli	cr7, r11, (PAGE_OFFSET + 0x1800000)@h
	patch_site	0b, patch__fixupdar_linmem_top

	/* create physical page address from effective address */
	tophys(r11, r10)
	blt-	cr7, 201f
	mfspr	r11, SPRN_M_TWB	/* Get level 1 table */
	rlwinm	r11, r11, 0, 20, 31
	oris	r11, r11, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
	lwz	r11, (swapper_pg_dir-PAGE_OFFSET)@l(r11)	/* Get the level 1 entry */
	mtspr	SPRN_MD_TWC, r11
	mtcr	r11
	mfspr	r11, SPRN_MD_TWC
	lwz	r11, 0(r11)	/* Get the pte */
	bt	28,200f		/* bit 28 = Large page (8M) */
	bt	29,202f		/* bit 29 = Large page (8M or 512K) */
	/* concat physical page address(r11) and page offset(r10) */
	rlwimi	r11, r10, 0, 32 - PAGE_SHIFT, 31
201:	lwz	r11,0(r11)
/* Check if it really is a dcbx instruction. */
/* dcbt and dcbtst does not generate DTLB Misses/Errors,
 * no need to include them here */
	xoris	r10, r11, 0x7c00	/* check if major OP code is 31 */
	rlwinm	r10, r10, 0, 21, 5
	cmpwi	cr0, r10, 2028	/* Is dcbz? */
	beq+	142f
	cmpwi	cr0, r10, 940	/* Is dcbi? */
	beq+	142f
	cmpwi	cr0, r10, 108	/* Is dcbst? */
	beq+	144f		/* Fix up store bit! */
	cmpwi	cr0, r10, 172	/* Is dcbf? */
	beq+	142f
	cmpwi	cr0, r10, 1964	/* Is icbi? */
	beq+	142f
141:	mfspr	r10,SPRN_M_TW
	b	DARFixed	/* Nope, go back to normal TLB processing */

200:
	/* concat physical page address(r11) and page offset(r10) */
	rlwimi	r11, r10, 0, 32 - PAGE_SHIFT_8M, 31
	b	201b

202:
	/* concat physical page address(r11) and page offset(r10) */
	rlwimi	r11, r10, 0, 32 - PAGE_SHIFT_512K, 31
	b	201b

144:	mfspr	r10, SPRN_DSISR
	rlwinm	r10, r10,0,7,5	/* Clear store bit for buggy dcbst insn */
	mtspr	SPRN_DSISR, r10
142:	/* continue, it was a dcbx, dcbi instruction. */
#ifndef NO_SELF_MODIFYING_CODE
	andis.	r10,r11,0x1f	/* test if reg RA is r0 */
	li	r10,modified_instr@l
	dcbtst	r0,r10		/* touch for store */
	rlwinm	r11,r11,0,0,20	/* Zero lower 10 bits */
	oris	r11,r11,640	/* Transform instr. to a "add r10,RA,RB" */
	ori	r11,r11,532
	stw	r11,0(r10)	/* store add/and instruction */
	dcbf	0,r10		/* flush new instr. to memory. */
	icbi	0,r10		/* invalidate instr. cache line */
	mfspr	r11, SPRN_SPRG_SCRATCH1	/* restore r11 */
	mfspr	r10, SPRN_SPRG_SCRATCH0	/* restore r10 */
	isync			/* Wait until new instr is loaded from memory */
modified_instr:
	.space	4		/* this is where the add instr. is stored */
	bne+	143f
	subf	r10,r0,r10	/* r10=r10-r0, only if reg RA is r0 */
143:	mtdar	r10		/* store faulting EA in DAR */
	mfspr	r10,SPRN_M_TW
	b	DARFixed	/* Go back to normal TLB handling */
#else
	mfctr	r10
	mtdar	r10			/* save ctr reg in DAR */
	rlwinm	r10, r11, 24, 24, 28	/* offset into jump table for reg RB */
	addi	r10, r10, 150f@l	/* add start of table */
	mtctr	r10			/* load ctr with jump address */
	xor	r10, r10, r10		/* sum starts at zero */
	bctr				/* jump into table */
150:
	add	r10, r10, r0	;b	151f
	add	r10, r10, r1	;b	151f
	add	r10, r10, r2	;b	151f
	add	r10, r10, r3	;b	151f
	add	r10, r10, r4	;b	151f
	add	r10, r10, r5	;b	151f
	add	r10, r10, r6	;b	151f
	add	r10, r10, r7	;b	151f
	add	r10, r10, r8	;b	151f
	add	r10, r10, r9	;b	151f
	mtctr	r11	;b	154f	/* r10 needs special handling */
	mtctr	r11	;b	153f	/* r11 needs special handling */
	add	r10, r10, r12	;b	151f
	add	r10, r10, r13	;b	151f
	add	r10, r10, r14	;b	151f
	add	r10, r10, r15	;b	151f
	add	r10, r10, r16	;b	151f
	add	r10, r10, r17	;b	151f
	add	r10, r10, r18	;b	151f
	add	r10, r10, r19	;b	151f
	add	r10, r10, r20	;b	151f
	add	r10, r10, r21	;b	151f
	add	r10, r10, r22	;b	151f
	add	r10, r10, r23	;b	151f
	add	r10, r10, r24	;b	151f
	add	r10, r10, r25	;b	151f
	add	r10, r10, r26	;b	151f
	add	r10, r10, r27	;b	151f
	add	r10, r10, r28	;b	151f
	add	r10, r10, r29	;b	151f
	add	r10, r10, r30	;b	151f
	add	r10, r10, r31
151:
	rlwinm. r11,r11,19,24,28	/* offset into jump table for reg RA */
	beq	152f			/* if reg RA is zero, don't add it */
	addi	r11, r11, 150b@l	/* add start of table */
	mtctr	r11			/* load ctr with jump address */
	rlwinm	r11,r11,0,16,10		/* make sure we don't execute this more than once */
	bctr				/* jump into table */
152:
	mfdar	r11
	mtctr	r11			/* restore ctr reg from DAR */
	mtdar	r10			/* save fault EA to DAR */
	mfspr	r10,SPRN_M_TW
	b	DARFixed		/* Go back to normal TLB handling */

	/* special handling for r10,r11 since these are modified already */
153:	mfspr	r11, SPRN_SPRG_SCRATCH1	/* load r11 from SPRN_SPRG_SCRATCH1 */
	add	r10, r10, r11	/* add it */
	mfctr	r11		/* restore r11 */
	b	151b
154:	mfspr	r11, SPRN_SPRG_SCRATCH0	/* load r10 from SPRN_SPRG_SCRATCH0 */
	add	r10, r10, r11	/* add it */
	mfctr	r11		/* restore r11 */
	b	151b
#endif

/*
 * This is where the main kernel code starts.
 */
start_here:
	/* ptr to current */
	lis	r2,init_task@h
	ori	r2,r2,init_task@l

	/* ptr to phys current thread */
	tophys(r4,r2)
	addi	r4,r4,THREAD	/* init task's THREAD */
	mtspr	SPRN_SPRG_THREAD,r4

	/* stack */
	lis	r1,init_thread_union@ha
	addi	r1,r1,init_thread_union@l
	li	r0,0
	stwu	r0,THREAD_SIZE-STACK_FRAME_OVERHEAD(r1)

	lis	r6, swapper_pg_dir@ha
	tophys(r6,r6)
	mtspr	SPRN_M_TWB, r6

	bl	early_init	/* We have to do this with MMU on */

/*
 * Decide what sort of machine this is and initialize the MMU.
 */
#ifdef CONFIG_KASAN
	bl	kasan_early_init
#endif
	li	r3,0
	mr	r4,r31
	bl	machine_init
	bl	MMU_init

/*
 * Go back to running unmapped so we can load up new values
 * and change to using our exception vectors.
 * On the 8xx, all we have to do is invalidate the TLB to clear
 * the old 8M byte TLB mappings and load the page table base register.
 */
	/* The right way to do this would be to track it down through
	 * init's THREAD like the context switch code does, but this is
	 * easier......until someone changes init's static structures.
	 */
	lis	r4,2f@h
	ori	r4,r4,2f@l
	tophys(r4,r4)
	li	r3,MSR_KERNEL & ~(MSR_IR|MSR_DR)
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	rfi
/* Load up the kernel context */
2:
	tlbia			/* Clear all TLB entries */
	sync			/* wait for tlbia/tlbie to finish */

	/* set up the PTE pointers for the Abatron bdiGDB.
	*/
	lis	r5, abatron_pteptrs@h
	ori	r5, r5, abatron_pteptrs@l
	stw	r5, 0xf0(0)	/* Must match your Abatron config file */
	tophys(r5,r5)
	lis	r6, swapper_pg_dir@h
	ori	r6, r6, swapper_pg_dir@l
	stw	r6, 0(r5)

/* Now turn on the MMU for real! */
	li	r4,MSR_KERNEL
	lis	r3,start_kernel@h
	ori	r3,r3,start_kernel@l
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfi			/* enable MMU and jump to start_kernel */

/* Set up the initial MMU state so we can do the first level of
 * kernel initialization.  This maps the first 8 MBytes of memory 1:1
 * virtual to physical.  Also, set the cache mode since that is defined
 * by TLB entries and perform any additional mapping (like of the IMMR).
 * If configured to pin some TLBs, we pin the first 8 Mbytes of kernel,
 * 24 Mbytes of data, and the 512k IMMR space.  Anything not covered by
 * these mappings is mapped by page tables.
 */
initial_mmu:
	li	r8, 0
	mtspr	SPRN_MI_CTR, r8		/* remove PINNED ITLB entries */
	lis	r10, MD_RESETVAL@h
#ifndef CONFIG_8xx_COPYBACK
	oris	r10, r10, MD_WTDEF@h
#endif
	mtspr	SPRN_MD_CTR, r10	/* remove PINNED DTLB entries */

	tlbia			/* Invalidate all TLB entries */
#ifdef CONFIG_PIN_TLB_DATA
	oris	r10, r10, MD_RSV4I@h
	mtspr	SPRN_MD_CTR, r10	/* Set data TLB control */
#endif

	lis	r8, MI_APG_INIT@h	/* Set protection modes */
	ori	r8, r8, MI_APG_INIT@l
	mtspr	SPRN_MI_AP, r8
	lis	r8, MD_APG_INIT@h
	ori	r8, r8, MD_APG_INIT@l
	mtspr	SPRN_MD_AP, r8

	/* Map a 512k page for the IMMR to get the processor
	 * internal registers (among other things).
	 */
#ifdef CONFIG_PIN_TLB_IMMR
	oris	r10, r10, MD_RSV4I@h
	ori	r10, r10, 0x1c00
	mtspr	SPRN_MD_CTR, r10

	mfspr	r9, 638			/* Get current IMMR */
	andis.	r9, r9, 0xfff8		/* Get 512 kbytes boundary */

	lis	r8, VIRT_IMMR_BASE@h	/* Create vaddr for TLB */
	ori	r8, r8, MD_EVALID	/* Mark it valid */
	mtspr	SPRN_MD_EPN, r8
	li	r8, MD_PS512K | MD_GUARDED	/* Set 512k byte page */
	ori	r8, r8, MD_SVALID	/* Make it valid */
	mtspr	SPRN_MD_TWC, r8
	mr	r8, r9			/* Create paddr for TLB */
	ori	r8, r8, MI_BOOTINIT|0x2 /* Inhibit cache -- Cort */
	mtspr	SPRN_MD_RPN, r8
#endif

	/* Now map the lower RAM (up to 32 Mbytes) into the ITLB. */
#ifdef CONFIG_PIN_TLB_TEXT
	lis	r8, MI_RSV4I@h
	ori	r8, r8, 0x1c00
#endif
	li	r9, 4				/* up to 4 pages of 8M */
	mtctr	r9
	lis	r9, KERNELBASE@h		/* Create vaddr for TLB */
	li	r10, MI_PS8MEG | MI_SVALID	/* Set 8M byte page */
	li	r11, MI_BOOTINIT		/* Create RPN for address 0 */
	lis	r12, _einittext@h
	ori	r12, r12, _einittext@l
1:
#ifdef CONFIG_PIN_TLB_TEXT
	mtspr	SPRN_MI_CTR, r8	/* Set instruction MMU control */
	addi	r8, r8, 0x100
#endif

	ori	r0, r9, MI_EVALID		/* Mark it valid */
	mtspr	SPRN_MI_EPN, r0
	mtspr	SPRN_MI_TWC, r10
	mtspr	SPRN_MI_RPN, r11		/* Store TLB entry */
	addis	r9, r9, 0x80
	addis	r11, r11, 0x80

	cmpl	cr0, r9, r12
	bdnzf	gt, 1b

	/* Since the cache is enabled according to the information we
	 * just loaded into the TLB, invalidate and enable the caches here.
	 * We should probably check/set other modes....later.
	 */
	lis	r8, IDC_INVALL@h
	mtspr	SPRN_IC_CST, r8
	mtspr	SPRN_DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	SPRN_IC_CST, r8
#ifdef CONFIG_8xx_COPYBACK
	mtspr	SPRN_DC_CST, r8
#else
	/* For a debug option, I left this here to easily enable
	 * the write through cache mode
	 */
	lis	r8, DC_SFWT@h
	mtspr	SPRN_DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	SPRN_DC_CST, r8
#endif
	/* Disable debug mode entry on breakpoints */
	mfspr	r8, SPRN_DER
#ifdef CONFIG_PERF_EVENTS
	rlwinm	r8, r8, 0, ~0xc
#else
	rlwinm	r8, r8, 0, ~0x8
#endif
	mtspr	SPRN_DER, r8
	blr


/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the data segment,
 * which is page-aligned.
 */
	.data
	.globl	sdata
sdata:
	.globl	empty_zero_page
	.align	PAGE_SHIFT
empty_zero_page:
	.space	PAGE_SIZE
EXPORT_SYMBOL(empty_zero_page)

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	PGD_TABLE_SIZE

/* Room for two PTE table poiners, usually the kernel and current user
 * pointer to their respective root page table (pgdir).
 */
	.globl	abatron_pteptrs
abatron_pteptrs:
	.space	8
