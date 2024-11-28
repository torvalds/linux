/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */

#include <linux/init.h>
#include <linux/magic.h>
#include <linux/pgtable.h>
#include <linux/sizes.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/export.h>
#include <asm/code-patching-asm.h>
#include <asm/interrupt.h>

/*
 * Value for the bits that have fixed value in RPN entries.
 * Also used for tagging DAR for DTLBerror.
 */
#define RPN_PATTERN	0x00f0

#include "head_32.h"

.macro compare_to_kernel_boundary scratch, addr
#if CONFIG_TASK_SIZE <= 0x80000000 && MODULES_VADDR >= 0x80000000
/* By simply checking Address >= 0x80000000, we know if its a kernel address */
	not.	\scratch, \addr
#else
	rlwinm	\scratch, \addr, 16, 0xfff8
	cmpli	cr0, \scratch, TASK_SIZE@h
#endif
.endm

#define PAGE_SHIFT_512K		19
#define PAGE_SHIFT_8M		23

	__HEAD
_GLOBAL(_stext);
_GLOBAL(_start);

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
	EXCEPTION(INTERRUPT_SYSTEM_RESET, Reset, system_reset_exception)

/* Machine check */
	START_EXCEPTION(INTERRUPT_MACHINE_CHECK, MachineCheck)
	EXCEPTION_PROLOG INTERRUPT_MACHINE_CHECK MachineCheck handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	machine_check_exception
	b	interrupt_return

/* External interrupt */
	EXCEPTION(INTERRUPT_EXTERNAL, HardwareInterrupt, do_IRQ)

/* Alignment exception */
	START_EXCEPTION(INTERRUPT_ALIGNMENT, Alignment)
	EXCEPTION_PROLOG INTERRUPT_ALIGNMENT Alignment handle_dar_dsisr=1
	prepare_transfer_to_handler
	bl	alignment_exception
	REST_NVGPRS(r1)
	b	interrupt_return

/* Program check exception */
	START_EXCEPTION(INTERRUPT_PROGRAM, ProgramCheck)
	EXCEPTION_PROLOG INTERRUPT_PROGRAM ProgramCheck
	prepare_transfer_to_handler
	bl	program_check_exception
	REST_NVGPRS(r1)
	b	interrupt_return

/* Decrementer */
	EXCEPTION(INTERRUPT_DECREMENTER, Decrementer, timer_interrupt)

/* System call */
	START_EXCEPTION(INTERRUPT_SYSCALL, SystemCall)
	SYSCALL_ENTRY	INTERRUPT_SYSCALL

/* Single step - not used on 601 */
	EXCEPTION(INTERRUPT_TRACE, SingleStep, single_step_exception)

/* On the MPC8xx, this is a software emulation interrupt.  It occurs
 * for all unimplemented and illegal instructions.
 */
	START_EXCEPTION(INTERRUPT_SOFT_EMU_8xx, SoftEmu)
	EXCEPTION_PROLOG INTERRUPT_SOFT_EMU_8xx SoftEmu
	prepare_transfer_to_handler
	bl	emulation_assist_interrupt
	REST_NVGPRS(r1)
	b	interrupt_return

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
#define INVALIDATE_ADJACENT_PAGES_CPU15(addr, tmp)	\
	addi	tmp, addr, PAGE_SIZE;	\
	tlbie	tmp;			\
	addi	tmp, addr, -PAGE_SIZE;	\
	tlbie	tmp
#else
#define INVALIDATE_ADJACENT_PAGES_CPU15(addr, tmp)
#endif

	START_EXCEPTION(INTERRUPT_INST_TLB_MISS_8xx, InstructionTLBMiss)
	mtspr	SPRN_SPRG_SCRATCH2, r10
	mtspr	SPRN_M_TW, r11

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	mfspr	r10, SPRN_SRR0	/* Get effective address of fault */
	INVALIDATE_ADJACENT_PAGES_CPU15(r10, r11)
	mtspr	SPRN_MD_EPN, r10
#ifdef CONFIG_MODULES
	mfcr	r11
	compare_to_kernel_boundary r10, r10
#endif
	mfspr	r10, SPRN_M_TWB	/* Get level 1 table */
#ifdef CONFIG_MODULES
	blt+	3f
	rlwinm	r10, r10, 0, 20, 31
	oris	r10, r10, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
	mtcr	r11
#endif
	lwz	r11, (swapper_pg_dir-PAGE_OFFSET)@l(r10)	/* Get level 1 entry */
	mtspr	SPRN_MD_TWC, r11
	mfspr	r10, SPRN_MD_TWC
	lwz	r10, 0(r10)	/* Get the pte */
	rlwimi	r11, r10, 0, _PAGE_GUARDED | _PAGE_ACCESSED
	rlwimi	r11, r10, 32 - 9, _PMD_PAGE_512K
	mtspr	SPRN_MI_TWC, r11
	/* The Linux PTE won't go exactly into the MMU TLB.
	 * Software indicator bits 20 and 23 must be clear.
	 * Software indicator bits 22, 24, 25, 26, and 27 must be
	 * set.  All other Linux PTE bits control the behavior
	 * of the MMU.
	 */
	rlwinm	r10, r10, 0, ~0x0f00	/* Clear bits 20-23 */
	rlwimi	r10, r10, 4, 0x0400	/* Copy _PAGE_EXEC into bit 21 */
	ori	r10, r10, RPN_PATTERN | 0x200 /* Set 22 and 24-27 */
	mtspr	SPRN_MI_RPN, r10	/* Update TLB entry */

	/* Restore registers */
0:	mfspr	r10, SPRN_SPRG_SCRATCH2
	mfspr	r11, SPRN_M_TW
	rfi
	patch_site	0b, patch__itlbmiss_exit_1

#ifdef CONFIG_PERF_EVENTS
	patch_site	0f, patch__itlbmiss_perf
0:	lwz	r10, (itlb_miss_counter - PAGE_OFFSET)@l(0)
	addi	r10, r10, 1
	stw	r10, (itlb_miss_counter - PAGE_OFFSET)@l(0)
	mfspr	r10, SPRN_SPRG_SCRATCH2
	mfspr	r11, SPRN_M_TW
	rfi
#endif

	START_EXCEPTION(INTERRUPT_DATA_TLB_MISS_8xx, DataStoreTLBMiss)
	mtspr	SPRN_SPRG_SCRATCH2, r10
	mtspr	SPRN_M_TW, r11
	mfcr	r11

	/* If we are faulting a kernel address, we have to use the
	 * kernel page tables.
	 */
	mfspr	r10, SPRN_MD_EPN
	compare_to_kernel_boundary r10, r10
	mfspr	r10, SPRN_M_TWB	/* Get level 1 table */
	blt+	3f
	rlwinm	r10, r10, 0, 20, 31
	oris	r10, r10, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
	mtcr	r11
	lwz	r11, (swapper_pg_dir-PAGE_OFFSET)@l(r10)	/* Get level 1 entry */

	mtspr	SPRN_MD_TWC, r11
	mfspr	r10, SPRN_MD_TWC
	lwz	r10, 0(r10)	/* Get the pte */

	/* Insert Guarded and Accessed flags into the TWC from the Linux PTE.
	 * It is bit 27 of both the Linux PTE and the TWC (at least
	 * I got that right :-).  It will be better when we can put
	 * this into the Linux pgd/pmd and load it in the operation
	 * above.
	 */
	rlwimi	r11, r10, 0, _PAGE_GUARDED | _PAGE_ACCESSED
	rlwimi	r11, r10, 32 - 9, _PMD_PAGE_512K
	mtspr	SPRN_MD_TWC, r11

	/* The Linux PTE won't go exactly into the MMU TLB.
	 * Software indicator bits 24, 25, 26, and 27 must be
	 * set.  All other Linux PTE bits control the behavior
	 * of the MMU.
	 */
	li	r11, RPN_PATTERN
	rlwimi	r10, r11, 0, 24, 27	/* Set 24-27 */
	mtspr	SPRN_MD_RPN, r10	/* Update TLB entry */
	mtspr	SPRN_DAR, r11		/* Tag DAR */

	/* Restore registers */

0:	mfspr	r10, SPRN_SPRG_SCRATCH2
	mfspr	r11, SPRN_M_TW
	rfi
	patch_site	0b, patch__dtlbmiss_exit_1

#ifdef CONFIG_PERF_EVENTS
	patch_site	0f, patch__dtlbmiss_perf
0:	lwz	r10, (dtlb_miss_counter - PAGE_OFFSET)@l(0)
	addi	r10, r10, 1
	stw	r10, (dtlb_miss_counter - PAGE_OFFSET)@l(0)
	mfspr	r10, SPRN_SPRG_SCRATCH2
	mfspr	r11, SPRN_M_TW
	rfi
#endif

/* This is an instruction TLB error on the MPC8xx.  This could be due
 * to many reasons, such as executing guarded memory or illegal instruction
 * addresses.  There is nothing to do but handle a big time error fault.
 */
	START_EXCEPTION(INTERRUPT_INST_TLB_ERROR_8xx, InstructionTLBError)
	/* 0x400 is InstructionAccess exception, needed by bad_page_fault() */
	EXCEPTION_PROLOG INTERRUPT_INST_STORAGE InstructionTLBError
	andis.	r5,r9,DSISR_SRR1_MATCH_32S@h /* Filter relevant SRR1 bits */
	andis.	r10,r9,SRR1_ISI_NOPT@h
	beq+	.Litlbie
	tlbie	r12
.Litlbie:
	stw	r12, _DAR(r11)
	stw	r5, _DSISR(r11)
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return

/* This is the data TLB error on the MPC8xx.  This could be due to
 * many reasons, including a dirty update to a pte.  We bail out to
 * a higher level function that can handle it.
 */
	START_EXCEPTION(INTERRUPT_DATA_TLB_ERROR_8xx, DataTLBError)
	EXCEPTION_PROLOG_0 handle_dar_dsisr=1
	mfspr	r11, SPRN_DAR
	cmpwi	cr1, r11, RPN_PATTERN
	beq-	cr1, FixupDAR	/* must be a buggy dcbX, icbi insn. */
DARFixed:/* Return from dcbx instruction bug workaround */
	EXCEPTION_PROLOG_1
	/* 0x300 is DataAccess exception, needed by bad_page_fault() */
	EXCEPTION_PROLOG_2 INTERRUPT_DATA_STORAGE DataTLBError handle_dar_dsisr=1
	lwz	r4, _DAR(r11)
	lwz	r5, _DSISR(r11)
	andis.	r10,r5,DSISR_NOHPTE@h
	beq+	.Ldtlbie
	tlbie	r4
.Ldtlbie:
	prepare_transfer_to_handler
	bl	do_page_fault
	b	interrupt_return

#ifdef CONFIG_VMAP_STACK
	vmap_stack_overflow_exception
#endif

/* On the MPC8xx, these next four traps are used for development
 * support of breakpoints and such.  Someday I will get around to
 * using them.
 */
	START_EXCEPTION(INTERRUPT_DATA_BREAKPOINT_8xx, DataBreakpoint)
	EXCEPTION_PROLOG_0 handle_dar_dsisr=1
	mfspr	r11, SPRN_SRR0
	cmplwi	cr1, r11, (.Ldtlbie - PAGE_OFFSET)@l
	cmplwi	cr7, r11, (.Litlbie - PAGE_OFFSET)@l
	cror	4*cr1+eq, 4*cr1+eq, 4*cr7+eq
	bne	cr1, 1f
	mtcr	r10
	mfspr	r10, SPRN_SPRG_SCRATCH0
	mfspr	r11, SPRN_SPRG_SCRATCH1
	rfi

1:	EXCEPTION_PROLOG_1
	EXCEPTION_PROLOG_2 INTERRUPT_DATA_BREAKPOINT_8xx DataBreakpoint handle_dar_dsisr=1
	mfspr	r4,SPRN_BAR
	stw	r4,_DAR(r11)
	prepare_transfer_to_handler
	bl	do_break
	REST_NVGPRS(r1)
	b	interrupt_return

#ifdef CONFIG_PERF_EVENTS
	START_EXCEPTION(INTERRUPT_INST_BREAKPOINT_8xx, InstructionBreakpoint)
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
	EXCEPTION(INTERRUPT_INST_BREAKPOINT_8xx, Trap_1d, unknown_exception)
#endif
	EXCEPTION(0x1e00, Trap_1e, unknown_exception)
	EXCEPTION(0x1f00, Trap_1f, unknown_exception)

	__HEAD
	. = 0x2000

/* This is the procedure to calculate the data EA for buggy dcbx,dcbi instructions
 * by decoding the registers used by the dcbx instruction and adding them.
 * DAR is set to the calculated address.
 */
FixupDAR:/* Entry point for dcbx workaround. */
	mtspr	SPRN_M_TW, r10
	/* fetch instruction from memory. */
	mfspr	r10, SPRN_SRR0
	mtspr	SPRN_MD_EPN, r10
	rlwinm	r11, r10, 16, 0xfff8
	cmpli	cr1, r11, TASK_SIZE@h
	mfspr	r11, SPRN_M_TWB	/* Get level 1 table */
	blt+	cr1, 3f

	/* create physical page address from effective address */
	tophys(r11, r10)
	mfspr	r11, SPRN_M_TWB	/* Get level 1 table */
	rlwinm	r11, r11, 0, 20, 31
	oris	r11, r11, (swapper_pg_dir - PAGE_OFFSET)@ha
3:
	lwz	r11, (swapper_pg_dir-PAGE_OFFSET)@l(r11)	/* Get the level 1 entry */
	mtspr	SPRN_MD_TWC, r11
	mtcrf	0x01, r11
	mfspr	r11, SPRN_MD_TWC
	lwz	r11, 0(r11)	/* Get the pte */
	bt	28,200f		/* bit 28 = Large page (8M) */
	/* concat physical page address(r11) and page offset(r10) */
	rlwimi	r11, r10, 0, 32 - PAGE_SHIFT, 31
201:	lwz	r11,0(r11)
/* Check if it really is a dcbx instruction. */
/* dcbt and dcbtst does not generate DTLB Misses/Errors,
 * no need to include them here */
	xoris	r10, r11, 0x7c00	/* check if major OP code is 31 */
	rlwinm	r10, r10, 0, 21, 5
	cmpwi	cr1, r10, 2028	/* Is dcbz? */
	beq+	cr1, 142f
	cmpwi	cr1, r10, 940	/* Is dcbi? */
	beq+	cr1, 142f
	cmpwi	cr1, r10, 108	/* Is dcbst? */
	beq+	cr1, 144f		/* Fix up store bit! */
	cmpwi	cr1, r10, 172	/* Is dcbf? */
	beq+	cr1, 142f
	cmpwi	cr1, r10, 1964	/* Is icbi? */
	beq+	cr1, 142f
141:	mfspr	r10,SPRN_M_TW
	b	DARFixed	/* Nope, go back to normal TLB processing */

200:
	/* concat physical page address(r11) and page offset(r10) */
	rlwimi	r11, r10, 0, 32 - PAGE_SHIFT_8M, 31
	b	201b

144:	mfspr	r10, SPRN_DSISR
	rlwinm	r10, r10,0,7,5	/* Clear store bit for buggy dcbst insn */
	mtspr	SPRN_DSISR, r10
142:	/* continue, it was a dcbx, dcbi instruction. */
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
	rlwinm	r11,r11,19,24,28	/* offset into jump table for reg RA */
	cmpwi	cr1, r11, 0
	beq	cr1, 152f		/* if reg RA is zero, don't add it */
	addi	r11, r11, 150b@l	/* add start of table */
	mtctr	r11			/* load ctr with jump address */
	rlwinm	r11,r11,0,16,10		/* make sure we don't execute this more than once */
	bctr				/* jump into table */
152:
	mfdar	r11
	mtctr	r11			/* restore ctr reg from DAR */
	mfspr	r11, SPRN_SPRG_THREAD
	stw	r10, DAR(r11)
	mfspr	r10, SPRN_DSISR
	stw	r10, DSISR(r11)
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
	lis	r0, STACK_END_MAGIC@h
	ori	r0, r0, STACK_END_MAGIC@l
	stw	r0, 0(r1)
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
#ifdef CONFIG_PIN_TLB_IMMR
	lis	r0, MD_TWAM@h
	oris	r0, r0, 0x1f00
	mtspr	SPRN_MD_CTR, r0
	LOAD_REG_IMMEDIATE(r0, VIRT_IMMR_BASE | MD_EVALID)
	tlbie	r0
	mtspr	SPRN_MD_EPN, r0
	LOAD_REG_IMMEDIATE(r0, MD_SVALID | MD_PS512K | MD_GUARDED)
	mtspr	SPRN_MD_TWC, r0
	mfspr   r0, SPRN_IMMR
	rlwinm	r0, r0, 0, 0xfff80000
	ori	r0, r0, 0xf0 | _PAGE_DIRTY | _PAGE_SPS | _PAGE_SH | \
			_PAGE_NO_CACHE | _PAGE_PRESENT
	mtspr	SPRN_MD_RPN, r0
	lis	r0, (MD_TWAM | MD_RSV4I)@h
	mtspr	SPRN_MD_CTR, r0
#endif
#if !defined(CONFIG_PIN_TLB_DATA) && !defined(CONFIG_PIN_TLB_IMMR)
	lis	r0, MD_TWAM@h
	mtspr	SPRN_MD_CTR, r0
#endif
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
	lis	r10, MD_TWAM@h
	mtspr	SPRN_MD_CTR, r10	/* remove PINNED DTLB entries */

	tlbia			/* Invalidate all TLB entries */

	lis	r8, MI_APG_INIT@h	/* Set protection modes */
	ori	r8, r8, MI_APG_INIT@l
	mtspr	SPRN_MI_AP, r8
	lis	r8, MD_APG_INIT@h
	ori	r8, r8, MD_APG_INIT@l
	mtspr	SPRN_MD_AP, r8

	/* Map the lower RAM (up to 32 Mbytes) into the ITLB and DTLB */
	lis	r8, MI_RSV4I@h
	ori	r8, r8, 0x1c00
	oris	r12, r10, MD_RSV4I@h
	ori	r12, r12, 0x1c00
	li	r9, 4				/* up to 4 pages of 8M */
	mtctr	r9
	lis	r9, KERNELBASE@h		/* Create vaddr for TLB */
	li	r10, MI_PS8MEG | _PMD_ACCESSED | MI_SVALID
	li	r11, MI_BOOTINIT		/* Create RPN for address 0 */
1:
	mtspr	SPRN_MI_CTR, r8	/* Set instruction MMU control */
	addi	r8, r8, 0x100
	ori	r0, r9, MI_EVALID		/* Mark it valid */
	mtspr	SPRN_MI_EPN, r0
	mtspr	SPRN_MI_TWC, r10
	mtspr	SPRN_MI_RPN, r11		/* Store TLB entry */
	mtspr	SPRN_MD_CTR, r12
	addi	r12, r12, 0x100
	mtspr	SPRN_MD_EPN, r0
	mtspr	SPRN_MD_TWC, r10
	mtspr	SPRN_MD_RPN, r11
	addis	r9, r9, 0x80
	addis	r11, r11, 0x80

	bdnz	1b

	/* Since the cache is enabled according to the information we
	 * just loaded into the TLB, invalidate and enable the caches here.
	 * We should probably check/set other modes....later.
	 */
	lis	r8, IDC_INVALL@h
	mtspr	SPRN_IC_CST, r8
	mtspr	SPRN_DC_CST, r8
	lis	r8, IDC_ENABLE@h
	mtspr	SPRN_IC_CST, r8
	mtspr	SPRN_DC_CST, r8
	/* Disable debug mode entry on breakpoints */
	mfspr	r8, SPRN_DER
#ifdef CONFIG_PERF_EVENTS
	rlwinm	r8, r8, 0, ~0xc
#else
	rlwinm	r8, r8, 0, ~0x8
#endif
	mtspr	SPRN_DER, r8
	blr

_GLOBAL(mmu_pin_tlb)
	lis	r9, (1f - PAGE_OFFSET)@h
	ori	r9, r9, (1f - PAGE_OFFSET)@l
	mfmsr	r10
	mflr	r11
	li	r12, MSR_KERNEL & ~(MSR_IR | MSR_DR | MSR_RI)
	rlwinm	r0, r10, 0, ~MSR_RI
	rlwinm	r0, r0, 0, ~MSR_EE
	mtmsr	r0
	isync
	.align	4
	mtspr	SPRN_SRR0, r9
	mtspr	SPRN_SRR1, r12
	rfi
1:
	li	r5, 0
	lis	r6, MD_TWAM@h
	mtspr	SPRN_MI_CTR, r5
	mtspr	SPRN_MD_CTR, r6
	tlbia

	LOAD_REG_IMMEDIATE(r5, 28 << 8)
	LOAD_REG_IMMEDIATE(r6, PAGE_OFFSET)
	LOAD_REG_IMMEDIATE(r7, MI_SVALID | MI_PS8MEG | _PMD_ACCESSED)
	LOAD_REG_IMMEDIATE(r8, 0xf0 | _PAGE_RO | _PAGE_SPS | _PAGE_SH | _PAGE_PRESENT)
	LOAD_REG_ADDR(r9, _sinittext)
	li	r0, 4
	mtctr	r0

2:	ori	r0, r6, MI_EVALID
	mtspr	SPRN_MI_CTR, r5
	mtspr	SPRN_MI_EPN, r0
	mtspr	SPRN_MI_TWC, r7
	mtspr	SPRN_MI_RPN, r8
	addi	r5, r5, 0x100
	addis	r6, r6, SZ_8M@h
	addis	r8, r8, SZ_8M@h
	cmplw	r6, r9
	bdnzt	lt, 2b
	lis	r0, MI_RSV4I@h
	mtspr	SPRN_MI_CTR, r0

	LOAD_REG_IMMEDIATE(r5, 28 << 8 | MD_TWAM)
#ifdef CONFIG_PIN_TLB_DATA
	LOAD_REG_IMMEDIATE(r6, PAGE_OFFSET)
	LOAD_REG_IMMEDIATE(r7, MI_SVALID | MI_PS8MEG | _PMD_ACCESSED)
	li	r8, 0
#ifdef CONFIG_PIN_TLB_IMMR
	li	r0, 3
#else
	li	r0, 4
#endif
	mtctr	r0
	cmpwi	r4, 0
	beq	4f
	LOAD_REG_ADDR(r9, _sinittext)

2:	ori	r0, r6, MD_EVALID
	ori	r12, r8, 0xf0 | _PAGE_RO | _PAGE_SPS | _PAGE_SH | _PAGE_PRESENT
	mtspr	SPRN_MD_CTR, r5
	mtspr	SPRN_MD_EPN, r0
	mtspr	SPRN_MD_TWC, r7
	mtspr	SPRN_MD_RPN, r12
	addi	r5, r5, 0x100
	addis	r6, r6, SZ_8M@h
	addis	r8, r8, SZ_8M@h
	cmplw	r6, r9
	bdnzt	lt, 2b
4:
2:	ori	r0, r6, MD_EVALID
	ori	r12, r8, 0xf0 | _PAGE_DIRTY | _PAGE_SPS | _PAGE_SH | _PAGE_PRESENT
	mtspr	SPRN_MD_CTR, r5
	mtspr	SPRN_MD_EPN, r0
	mtspr	SPRN_MD_TWC, r7
	mtspr	SPRN_MD_RPN, r12
	addi	r5, r5, 0x100
	addis	r6, r6, SZ_8M@h
	addis	r8, r8, SZ_8M@h
	cmplw	r6, r3
	bdnzt	lt, 2b
#endif
#ifdef CONFIG_PIN_TLB_IMMR
	LOAD_REG_IMMEDIATE(r0, VIRT_IMMR_BASE | MD_EVALID)
	LOAD_REG_IMMEDIATE(r7, MD_SVALID | MD_PS512K | MD_GUARDED | _PMD_ACCESSED)
	mfspr   r8, SPRN_IMMR
	rlwinm	r8, r8, 0, 0xfff80000
	ori	r8, r8, 0xf0 | _PAGE_DIRTY | _PAGE_SPS | _PAGE_SH | \
			_PAGE_NO_CACHE | _PAGE_PRESENT
	mtspr	SPRN_MD_CTR, r5
	mtspr	SPRN_MD_EPN, r0
	mtspr	SPRN_MD_TWC, r7
	mtspr	SPRN_MD_RPN, r8
#endif
#if defined(CONFIG_PIN_TLB_IMMR) || defined(CONFIG_PIN_TLB_DATA)
	lis	r0, (MD_RSV4I | MD_TWAM)@h
	mtspr	SPRN_MD_CTR, r0
#endif
	mtspr	SPRN_SRR1, r10
	mtspr	SPRN_SRR0, r11
	rfi
