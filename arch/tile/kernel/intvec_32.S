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
 * Linux interrupt vectors.
 */

#include <linux/linkage.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/atomic.h>
#include <asm/asm-offsets.h>
#include <hv/hypervisor.h>
#include <arch/abi.h>
#include <arch/interrupts.h>
#include <arch/spr_def.h>

#ifdef CONFIG_PREEMPT
# error "No support for kernel preemption currently"
#endif

#define PTREGS_PTR(reg, ptreg) addli reg, sp, C_ABI_SAVE_AREA_SIZE + (ptreg)

#define PTREGS_OFFSET_SYSCALL PTREGS_OFFSET_REG(TREG_SYSCALL_NR)

#if !CHIP_HAS_WH64()
	/* By making this an empty macro, we can use wh64 in the code. */
	.macro  wh64 reg
	.endm
#endif

	.macro  push_reg reg, ptr=sp, delta=-4
	{
	 sw     \ptr, \reg
	 addli  \ptr, \ptr, \delta
	}
	.endm

	.macro  pop_reg reg, ptr=sp, delta=4
	{
	 lw     \reg, \ptr
	 addli  \ptr, \ptr, \delta
	}
	.endm

	.macro  pop_reg_zero reg, zreg, ptr=sp, delta=4
	{
	 move   \zreg, zero
	 lw     \reg, \ptr
	 addi   \ptr, \ptr, \delta
	}
	.endm

	.macro  push_extra_callee_saves reg
	PTREGS_PTR(\reg, PTREGS_OFFSET_REG(51))
	push_reg r51, \reg
	push_reg r50, \reg
	push_reg r49, \reg
	push_reg r48, \reg
	push_reg r47, \reg
	push_reg r46, \reg
	push_reg r45, \reg
	push_reg r44, \reg
	push_reg r43, \reg
	push_reg r42, \reg
	push_reg r41, \reg
	push_reg r40, \reg
	push_reg r39, \reg
	push_reg r38, \reg
	push_reg r37, \reg
	push_reg r36, \reg
	push_reg r35, \reg
	push_reg r34, \reg, PTREGS_OFFSET_BASE - PTREGS_OFFSET_REG(34)
	.endm

	.macro  panic str
	.pushsection .rodata, "a"
1:
	.asciz  "\str"
	.popsection
	{
	 moveli r0, lo16(1b)
	}
	{
	 auli   r0, r0, ha16(1b)
	 jal    panic
	}
	.endm

#ifdef __COLLECT_LINKER_FEEDBACK__
	.pushsection .text.intvec_feedback,"ax"
intvec_feedback:
	.popsection
#endif

	/*
	 * Default interrupt handler.
	 *
	 * vecnum is where we'll put this code.
	 * c_routine is the C routine we'll call.
	 *
	 * The C routine is passed two arguments:
	 * - A pointer to the pt_regs state.
	 * - The interrupt vector number.
	 *
	 * The "processing" argument specifies the code for processing
	 * the interrupt. Defaults to "handle_interrupt".
	 */
	.macro  int_hand vecnum, vecname, c_routine, processing=handle_interrupt
	.org    (\vecnum << 8)
intvec_\vecname:
	.ifc    \vecnum, INT_SWINT_1
	blz     TREG_SYSCALL_NR_NAME, sys_cmpxchg
	.endif

	/* Temporarily save a register so we have somewhere to work. */

	mtspr   SPR_SYSTEM_SAVE_K_1, r0
	mfspr   r0, SPR_EX_CONTEXT_K_1

	/* The cmpxchg code clears sp to force us to reset it here on fault. */
	{
	 bz     sp, 2f
	 andi   r0, r0, SPR_EX_CONTEXT_1_1__PL_MASK  /* mask off ICS */
	}

	.ifc    \vecnum, INT_DOUBLE_FAULT
	/*
	 * For double-faults from user-space, fall through to the normal
	 * register save and stack setup path.  Otherwise, it's the
	 * hypervisor giving us one last chance to dump diagnostics, and we
	 * branch to the kernel_double_fault routine to do so.
	 */
	bz      r0, 1f
	j       _kernel_double_fault
1:
	.else
	/*
	 * If we're coming from user-space, then set sp to the top of
	 * the kernel stack.  Otherwise, assume sp is already valid.
	 */
	{
	 bnz    r0, 0f
	 move   r0, sp
	}
	.endif

	.ifc    \c_routine, do_page_fault
	/*
	 * The page_fault handler may be downcalled directly by the
	 * hypervisor even when Linux is running and has ICS set.
	 *
	 * In this case the contents of EX_CONTEXT_K_1 reflect the
	 * previous fault and can't be relied on to choose whether or
	 * not to reinitialize the stack pointer.  So we add a test
	 * to see whether SYSTEM_SAVE_K_2 has the high bit set,
	 * and if so we don't reinitialize sp, since we must be coming
	 * from Linux.  (In fact the precise case is !(val & ~1),
	 * but any Linux PC has to have the high bit set.)
	 *
	 * Note that the hypervisor *always* sets SYSTEM_SAVE_K_2 for
	 * any path that turns into a downcall to one of our TLB handlers.
	 */
	mfspr   r0, SPR_SYSTEM_SAVE_K_2
	{
	 blz    r0, 0f    /* high bit in S_S_1_2 is for a PC to use */
	 move   r0, sp
	}
	.endif

2:
	/*
	 * SYSTEM_SAVE_K_0 holds the cpu number in the low bits, and
	 * the current stack top in the higher bits.  So we recover
	 * our stack top by just masking off the low bits, then
	 * point sp at the top aligned address on the actual stack page.
	 */
	mfspr   r0, SPR_SYSTEM_SAVE_K_0
	mm      r0, r0, zero, LOG2_THREAD_SIZE, 31

0:
	/*
	 * Align the stack mod 64 so we can properly predict what
	 * cache lines we need to write-hint to reduce memory fetch
	 * latency as we enter the kernel.  The layout of memory is
	 * as follows, with cache line 0 at the lowest VA, and cache
	 * line 4 just below the r0 value this "andi" computes.
	 * Note that we never write to cache line 4, and we skip
	 * cache line 1 for syscalls.
	 *
	 *    cache line 4: ptregs padding (two words)
	 *    cache line 3: r46...lr, pc, ex1, faultnum, orig_r0, flags, pad
	 *    cache line 2: r30...r45
	 *    cache line 1: r14...r29
	 *    cache line 0: 2 x frame, r0..r13
	 */
	andi    r0, r0, -64

	/*
	 * Push the first four registers on the stack, so that we can set
	 * them to vector-unique values before we jump to the common code.
	 *
	 * Registers are pushed on the stack as a struct pt_regs,
	 * with the sp initially just above the struct, and when we're
	 * done, sp points to the base of the struct, minus
	 * C_ABI_SAVE_AREA_SIZE, so we can directly jal to C code.
	 *
	 * This routine saves just the first four registers, plus the
	 * stack context so we can do proper backtracing right away,
	 * and defers to handle_interrupt to save the rest.
	 * The backtracer needs pc, ex1, lr, sp, r52, and faultnum.
	 */
	addli   r0, r0, PTREGS_OFFSET_LR - (PTREGS_SIZE + KSTK_PTREGS_GAP)
	wh64    r0    /* cache line 3 */
	{
	 sw     r0, lr
	 addli  r0, r0, PTREGS_OFFSET_SP - PTREGS_OFFSET_LR
	}
	{
	 sw     r0, sp
	 addli  sp, r0, PTREGS_OFFSET_REG(52) - PTREGS_OFFSET_SP
	}
	{
	 sw     sp, r52
	 addli  sp, sp, PTREGS_OFFSET_REG(1) - PTREGS_OFFSET_REG(52)
	}
	wh64    sp    /* cache line 0 */
	{
	 sw     sp, r1
	 addli  sp, sp, PTREGS_OFFSET_REG(2) - PTREGS_OFFSET_REG(1)
	}
	{
	 sw     sp, r2
	 addli  sp, sp, PTREGS_OFFSET_REG(3) - PTREGS_OFFSET_REG(2)
	}
	{
	 sw     sp, r3
	 addli  sp, sp, PTREGS_OFFSET_PC - PTREGS_OFFSET_REG(3)
	}
	mfspr   r0, SPR_EX_CONTEXT_K_0
	.ifc \processing,handle_syscall
	/*
	 * Bump the saved PC by one bundle so that when we return, we won't
	 * execute the same swint instruction again.  We need to do this while
	 * we're in the critical section.
	 */
	addi    r0, r0, 8
	.endif
	{
	 sw     sp, r0
	 addli  sp, sp, PTREGS_OFFSET_EX1 - PTREGS_OFFSET_PC
	}
	mfspr   r0, SPR_EX_CONTEXT_K_1
	{
	 sw     sp, r0
	 addi   sp, sp, PTREGS_OFFSET_FAULTNUM - PTREGS_OFFSET_EX1
	/*
	 * Use r0 for syscalls so it's a temporary; use r1 for interrupts
	 * so that it gets passed through unchanged to the handler routine.
	 * Note that the .if conditional confusingly spans bundles.
	 */
	 .ifc \processing,handle_syscall
	 movei  r0, \vecnum
	}
	{
	 sw     sp, r0
	 .else
	 movei  r1, \vecnum
	}
	{
	 sw     sp, r1
	 .endif
	 addli  sp, sp, PTREGS_OFFSET_REG(0) - PTREGS_OFFSET_FAULTNUM
	}
	mfspr   r0, SPR_SYSTEM_SAVE_K_1    /* Original r0 */
	{
	 sw     sp, r0
	 addi   sp, sp, -PTREGS_OFFSET_REG(0) - 4
	}
	{
	 sw     sp, zero        /* write zero into "Next SP" frame pointer */
	 addi   sp, sp, -4      /* leave SP pointing at bottom of frame */
	}
	.ifc \processing,handle_syscall
	j       handle_syscall
	.else
	/*
	 * Capture per-interrupt SPR context to registers.
	 * We overload the meaning of r3 on this path such that if its bit 31
	 * is set, we have to mask all interrupts including NMIs before
	 * clearing the interrupt critical section bit.
	 * See discussion below at "finish_interrupt_save".
	 */
	.ifc \c_routine, do_page_fault
	mfspr   r2, SPR_SYSTEM_SAVE_K_3   /* address of page fault */
	mfspr   r3, SPR_SYSTEM_SAVE_K_2   /* info about page fault */
	.else
	.ifc \vecnum, INT_DOUBLE_FAULT
	{
	 mfspr  r2, SPR_SYSTEM_SAVE_K_2   /* double fault info from HV */
	 movei  r3, 0
	}
	.else
	.ifc \c_routine, do_trap
	{
	 mfspr  r2, GPV_REASON
	 movei  r3, 0
	}
	.else
	.ifc \c_routine, op_handle_perf_interrupt
	{
	 mfspr  r2, PERF_COUNT_STS
	 movei  r3, -1   /* not used, but set for consistency */
	}
	.else
#if CHIP_HAS_AUX_PERF_COUNTERS()
	.ifc \c_routine, op_handle_aux_perf_interrupt
	{
	 mfspr  r2, AUX_PERF_COUNT_STS
	 movei  r3, -1   /* not used, but set for consistency */
	}
	.else
#endif
	movei   r3, 0
#if CHIP_HAS_AUX_PERF_COUNTERS()
	.endif
#endif
	.endif
	.endif
	.endif
	.endif
	/* Put function pointer in r0 */
	moveli  r0, lo16(\c_routine)
	{
	 auli   r0, r0, ha16(\c_routine)
	 j       \processing
	}
	.endif
	ENDPROC(intvec_\vecname)

#ifdef __COLLECT_LINKER_FEEDBACK__
	.pushsection .text.intvec_feedback,"ax"
	.org    (\vecnum << 5)
	FEEDBACK_ENTER_EXPLICIT(intvec_\vecname, .intrpt1, 1 << 8)
	jrp     lr
	.popsection
#endif

	.endm


	/*
	 * Save the rest of the registers that we didn't save in the actual
	 * vector itself.  We can't use r0-r10 inclusive here.
	 */
	.macro  finish_interrupt_save, function

	/* If it's a syscall, save a proper orig_r0, otherwise just zero. */
	PTREGS_PTR(r52, PTREGS_OFFSET_ORIG_R0)
	{
	 .ifc \function,handle_syscall
	 sw     r52, r0
	 .else
	 sw     r52, zero
	 .endif
	 PTREGS_PTR(r52, PTREGS_OFFSET_TP)
	}

	/*
	 * For ordinary syscalls, we save neither caller- nor callee-
	 * save registers, since the syscall invoker doesn't expect the
	 * caller-saves to be saved, and the called kernel functions will
	 * take care of saving the callee-saves for us.
	 *
	 * For interrupts we save just the caller-save registers.  Saving
	 * them is required (since the "caller" can't save them).  Again,
	 * the called kernel functions will restore the callee-save
	 * registers for us appropriately.
	 *
	 * On return, we normally restore nothing special for syscalls,
	 * and just the caller-save registers for interrupts.
	 *
	 * However, there are some important caveats to all this:
	 *
	 * - We always save a few callee-save registers to give us
	 *   some scratchpad registers to carry across function calls.
	 *
	 * - fork/vfork/etc require us to save all the callee-save
	 *   registers, which we do in PTREGS_SYSCALL_ALL_REGS, below.
	 *
	 * - We always save r0..r5 and r10 for syscalls, since we need
	 *   to reload them a bit later for the actual kernel call, and
	 *   since we might need them for -ERESTARTNOINTR, etc.
	 *
	 * - Before invoking a signal handler, we save the unsaved
	 *   callee-save registers so they are visible to the
	 *   signal handler or any ptracer.
	 *
	 * - If the unsaved callee-save registers are modified, we set
	 *   a bit in pt_regs so we know to reload them from pt_regs
	 *   and not just rely on the kernel function unwinding.
	 *   (Done for ptrace register writes and SA_SIGINFO handler.)
	 */
	{
	 sw     r52, tp
	 PTREGS_PTR(r52, PTREGS_OFFSET_REG(33))
	}
	wh64    r52    /* cache line 2 */
	push_reg r33, r52
	push_reg r32, r52
	push_reg r31, r52
	.ifc \function,handle_syscall
	push_reg r30, r52, PTREGS_OFFSET_SYSCALL - PTREGS_OFFSET_REG(30)
	push_reg TREG_SYSCALL_NR_NAME, r52, \
	  PTREGS_OFFSET_REG(5) - PTREGS_OFFSET_SYSCALL
	.else

	push_reg r30, r52, PTREGS_OFFSET_REG(29) - PTREGS_OFFSET_REG(30)
	wh64    r52    /* cache line 1 */
	push_reg r29, r52
	push_reg r28, r52
	push_reg r27, r52
	push_reg r26, r52
	push_reg r25, r52
	push_reg r24, r52
	push_reg r23, r52
	push_reg r22, r52
	push_reg r21, r52
	push_reg r20, r52
	push_reg r19, r52
	push_reg r18, r52
	push_reg r17, r52
	push_reg r16, r52
	push_reg r15, r52
	push_reg r14, r52
	push_reg r13, r52
	push_reg r12, r52
	push_reg r11, r52
	push_reg r10, r52
	push_reg r9, r52
	push_reg r8, r52
	push_reg r7, r52
	push_reg r6, r52

	.endif

	push_reg r5, r52
	sw      r52, r4

	/* Load tp with our per-cpu offset. */
#ifdef CONFIG_SMP
	{
	 mfspr  r20, SPR_SYSTEM_SAVE_K_0
	 moveli r21, lo16(__per_cpu_offset)
	}
	{
	 auli   r21, r21, ha16(__per_cpu_offset)
	 mm     r20, r20, zero, 0, LOG2_THREAD_SIZE-1
	}
	s2a     r20, r20, r21
	lw      tp, r20
#else
	move    tp, zero
#endif

	/*
	 * If we will be returning to the kernel, we will need to
	 * reset the interrupt masks to the state they had before.
	 * Set DISABLE_IRQ in flags iff we came from PL1 with irqs disabled.
	 * We load flags in r32 here so we can jump to .Lrestore_regs
	 * directly after do_page_fault_ics() if necessary.
	 */
	mfspr   r32, SPR_EX_CONTEXT_K_1
	{
	 andi   r32, r32, SPR_EX_CONTEXT_1_1__PL_MASK  /* mask off ICS */
	 PTREGS_PTR(r21, PTREGS_OFFSET_FLAGS)
	}
	bzt     r32, 1f       /* zero if from user space */
	IRQS_DISABLED(r32)    /* zero if irqs enabled */
#if PT_FLAGS_DISABLE_IRQ != 1
# error Value of IRQS_DISABLED used to set PT_FLAGS_DISABLE_IRQ; fix
#endif
1:
	.ifnc \function,handle_syscall
	/* Record the fact that we saved the caller-save registers above. */
	ori     r32, r32, PT_FLAGS_CALLER_SAVES
	.endif
	sw      r21, r32

#ifdef __COLLECT_LINKER_FEEDBACK__
	/*
	 * Notify the feedback routines that we were in the
	 * appropriate fixed interrupt vector area.  Note that we
	 * still have ICS set at this point, so we can't invoke any
	 * atomic operations or we will panic.  The feedback
	 * routines internally preserve r0..r10 and r30 up.
	 */
	.ifnc \function,handle_syscall
	shli    r20, r1, 5
	.else
	moveli  r20, INT_SWINT_1 << 5
	.endif
	addli   r20, r20, lo16(intvec_feedback)
	auli    r20, r20, ha16(intvec_feedback)
	jalr    r20

	/* And now notify the feedback routines that we are here. */
	FEEDBACK_ENTER(\function)
#endif

	/*
	 * we've captured enough state to the stack (including in
	 * particular our EX_CONTEXT state) that we can now release
	 * the interrupt critical section and replace it with our
	 * standard "interrupts disabled" mask value.  This allows
	 * synchronous interrupts (and profile interrupts) to punch
	 * through from this point onwards.
	 *
	 * If bit 31 of r3 is set during a non-NMI interrupt, we know we
	 * are on the path where the hypervisor has punched through our
	 * ICS with a page fault, so we call out to do_page_fault_ics()
	 * to figure out what to do with it.  If the fault was in
	 * an atomic op, we unlock the atomic lock, adjust the
	 * saved register state a little, and return "zero" in r4,
	 * falling through into the normal page-fault interrupt code.
	 * If the fault was in a kernel-space atomic operation, then
	 * do_page_fault_ics() resolves it itself, returns "one" in r4,
	 * and as a result goes directly to restoring registers and iret,
	 * without trying to adjust the interrupt masks at all.
	 * The do_page_fault_ics() API involves passing and returning
	 * a five-word struct (in registers) to avoid writing the
	 * save and restore code here.
	 */
	.ifc \function,handle_nmi
	IRQ_DISABLE_ALL(r20)
	.else
	.ifnc \function,handle_syscall
	bgezt   r3, 1f
	{
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	 jal    do_page_fault_ics
	}
	FEEDBACK_REENTER(\function)
	bzt     r4, 1f
	j       .Lrestore_regs
1:
	.endif
	IRQ_DISABLE(r20, r21)
	.endif
	mtspr   INTERRUPT_CRITICAL_SECTION, zero

#if CHIP_HAS_WH64()
	/*
	 * Prepare the first 256 stack bytes to be rapidly accessible
	 * without having to fetch the background data.  We don't really
	 * know how far to write-hint, but kernel stacks generally
	 * aren't that big, and write-hinting here does take some time.
	 */
	addi    r52, sp, -64
	{
	 wh64   r52
	 addi   r52, r52, -64
	}
	{
	 wh64   r52
	 addi   r52, r52, -64
	}
	{
	 wh64   r52
	 addi   r52, r52, -64
	}
	wh64    r52
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	.ifnc \function,handle_nmi
	/*
	 * We finally have enough state set up to notify the irq
	 * tracing code that irqs were disabled on entry to the handler.
	 * The TRACE_IRQS_OFF call clobbers registers r0-r29.
	 * For syscalls, we already have the register state saved away
	 * on the stack, so we don't bother to do any register saves here,
	 * and later we pop the registers back off the kernel stack.
	 * For interrupt handlers, save r0-r3 in callee-saved registers.
	 */
	.ifnc \function,handle_syscall
	{ move r30, r0; move r31, r1 }
	{ move r32, r2; move r33, r3 }
	.endif
	TRACE_IRQS_OFF
	.ifnc \function,handle_syscall
	{ move r0, r30; move r1, r31 }
	{ move r2, r32; move r3, r33 }
	.endif
	.endif
#endif

	.endm

	.macro  check_single_stepping, kind, not_single_stepping
	/*
	 * Check for single stepping in user-level priv
	 *   kind can be "normal", "ill", or "syscall"
	 * At end, if fall-thru
	 *   r29: thread_info->step_state
	 *   r28: &pt_regs->pc
	 *   r27: pt_regs->pc
	 *   r26: thread_info->step_state->buffer
	 */

	/* Check for single stepping */
	GET_THREAD_INFO(r29)
	{
	 /* Get pointer to field holding step state */
	 addi   r29, r29, THREAD_INFO_STEP_STATE_OFFSET

	 /* Get pointer to EX1 in register state */
	 PTREGS_PTR(r27, PTREGS_OFFSET_EX1)
	}
	{
	 /* Get pointer to field holding PC */
	 PTREGS_PTR(r28, PTREGS_OFFSET_PC)

	 /* Load the pointer to the step state */
	 lw     r29, r29
	}
	/* Load EX1 */
	lw      r27, r27
	{
	 /* Points to flags */
	 addi   r23, r29, SINGLESTEP_STATE_FLAGS_OFFSET

	 /* No single stepping if there is no step state structure */
	 bzt    r29, \not_single_stepping
	}
	{
	 /* mask off ICS and any other high bits */
	 andi   r27, r27, SPR_EX_CONTEXT_1_1__PL_MASK

	 /* Load pointer to single step instruction buffer */
	 lw     r26, r29
	}
	/* Check priv state */
	bnz     r27, \not_single_stepping

	/* Get flags */
	lw      r22, r23
	{
	 /* Branch if single-step mode not enabled */
	 bbnst  r22, \not_single_stepping

	 /* Clear enabled flag */
	 andi   r22, r22, ~SINGLESTEP_STATE_MASK_IS_ENABLED
	}
	.ifc \kind,normal
	{
	 /* Load PC */
	 lw     r27, r28

	 /* Point to the entry containing the original PC */
	 addi   r24, r29, SINGLESTEP_STATE_ORIG_PC_OFFSET
	}
	{
	 /* Disable single stepping flag */
	 sw     r23, r22
	}
	{
	 /* Get the original pc */
	 lw     r24, r24

	 /* See if the PC is at the start of the single step buffer */
	 seq    r25, r26, r27
	}
	/*
	 * NOTE: it is really expected that the PC be in the single step buffer
	 *       at this point
	 */
	bzt     r25, \not_single_stepping

	/* Restore the original PC */
	sw      r28, r24
	.else
	.ifc \kind,syscall
	{
	 /* Load PC */
	 lw     r27, r28

	 /* Point to the entry containing the next PC */
	 addi   r24, r29, SINGLESTEP_STATE_NEXT_PC_OFFSET
	}
	{
	 /* Increment the stopped PC by the bundle size */
	 addi   r26, r26, 8

	 /* Disable single stepping flag */
	 sw     r23, r22
	}
	{
	 /* Get the next pc */
	 lw     r24, r24

	 /*
	  * See if the PC is one bundle past the start of the
	  * single step buffer
	  */
	 seq    r25, r26, r27
	}
	{
	 /*
	  * NOTE: it is really expected that the PC be in the
	  * single step buffer at this point
	  */
	 bzt    r25, \not_single_stepping
	}
	/* Set to the next PC */
	sw      r28, r24
	.else
	{
	 /* Point to 3rd bundle in buffer */
	 addi   r25, r26, 16

	 /* Load PC */
	 lw      r27, r28
	}
	{
	 /* Disable single stepping flag */
	 sw      r23, r22

	 /* See if the PC is in the single step buffer */
	 slte_u  r24, r26, r27
	}
	{
	 slte_u r25, r27, r25

	 /*
	  * NOTE: it is really expected that the PC be in the
	  * single step buffer at this point
	  */
	 bzt    r24, \not_single_stepping
	}
	bzt     r25, \not_single_stepping
	.endif
	.endif
	.endm

	/*
	 * Redispatch a downcall.
	 */
	.macro  dc_dispatch vecnum, vecname
	.org    (\vecnum << 8)
intvec_\vecname:
	j       hv_downcall_dispatch
	ENDPROC(intvec_\vecname)
	.endm

	/*
	 * Common code for most interrupts.  The C function we're eventually
	 * going to is in r0, and the faultnum is in r1; the original
	 * values for those registers are on the stack.
	 */
	.pushsection .text.handle_interrupt,"ax"
handle_interrupt:
	finish_interrupt_save handle_interrupt

	/*
	 * Check for if we are single stepping in user level. If so, then
	 * we need to restore the PC.
	 */

	check_single_stepping normal, .Ldispatch_interrupt
.Ldispatch_interrupt:

	/* Jump to the C routine; it should enable irqs as soon as possible. */
	{
	 jalr   r0
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	}
	FEEDBACK_REENTER(handle_interrupt)
	{
	 movei  r30, 0   /* not an NMI */
	 j      interrupt_return
	}
	STD_ENDPROC(handle_interrupt)

/*
 * This routine takes a boolean in r30 indicating if this is an NMI.
 * If so, we also expect a boolean in r31 indicating whether to
 * re-enable the oprofile interrupts.
 */
STD_ENTRY(interrupt_return)
	/* If we're resuming to kernel space, don't check thread flags. */
	{
	 bnz    r30, .Lrestore_all  /* NMIs don't special-case user-space */
	 PTREGS_PTR(r29, PTREGS_OFFSET_EX1)
	}
	lw      r29, r29
	andi    r29, r29, SPR_EX_CONTEXT_1_1__PL_MASK  /* mask off ICS */
	{
	 bzt    r29, .Lresume_userspace
	 PTREGS_PTR(r29, PTREGS_OFFSET_PC)
	}

	/* If we're resuming to _cpu_idle_nap, bump PC forward by 8. */
	{
	 lw     r28, r29
	 moveli r27, lo16(_cpu_idle_nap)
	}
	{
	 auli   r27, r27, ha16(_cpu_idle_nap)
	}
	{
	 seq    r27, r27, r28
	}
	{
	 bbns   r27, .Lrestore_all
	 addi   r28, r28, 8
	}
	sw      r29, r28
	j       .Lrestore_all

.Lresume_userspace:
	FEEDBACK_REENTER(interrupt_return)

	/*
	 * Disable interrupts so as to make sure we don't
	 * miss an interrupt that sets any of the thread flags (like
	 * need_resched or sigpending) between sampling and the iret.
	 * Routines like schedule() or do_signal() may re-enable
	 * interrupts before returning.
	 */
	IRQ_DISABLE(r20, r21)
	TRACE_IRQS_OFF  /* Note: clobbers registers r0-r29 */

	/* Get base of stack in r32; note r30/31 are used as arguments here. */
	GET_THREAD_INFO(r32)


	/* Check to see if there is any work to do before returning to user. */
	{
	 addi   r29, r32, THREAD_INFO_FLAGS_OFFSET
	 moveli r28, lo16(_TIF_ALLWORK_MASK)
	}
	{
	 lw     r29, r29
	 auli   r28, r28, ha16(_TIF_ALLWORK_MASK)
	}
	and     r28, r29, r28
	bnz     r28, .Lwork_pending

	/*
	 * In the NMI case we
	 * omit the call to single_process_check_nohz, which normally checks
	 * to see if we should start or stop the scheduler tick, because
	 * we can't call arbitrary Linux code from an NMI context.
	 * We always call the homecache TLB deferral code to re-trigger
	 * the deferral mechanism.
	 *
	 * The other chunk of responsibility this code has is to reset the
	 * interrupt masks appropriately to reset irqs and NMIs.  We have
	 * to call TRACE_IRQS_OFF and TRACE_IRQS_ON to support all the
	 * lockdep-type stuff, but we can't set ICS until afterwards, since
	 * ICS can only be used in very tight chunks of code to avoid
	 * tripping over various assertions that it is off.
	 *
	 * (There is what looks like a window of vulnerability here since
	 * we might take a profile interrupt between the two SPR writes
	 * that set the mask, but since we write the low SPR word first,
	 * and our interrupt entry code checks the low SPR word, any
	 * profile interrupt will actually disable interrupts in both SPRs
	 * before returning, which is OK.)
	 */
.Lrestore_all:
	PTREGS_PTR(r0, PTREGS_OFFSET_EX1)
	{
	 lw     r0, r0
	 PTREGS_PTR(r32, PTREGS_OFFSET_FLAGS)
	}
	{
	 andi   r0, r0, SPR_EX_CONTEXT_1_1__PL_MASK
	 lw     r32, r32
	}
	bnz    r0, 1f
	j       2f
#if PT_FLAGS_DISABLE_IRQ != 1
# error Assuming PT_FLAGS_DISABLE_IRQ == 1 so we can use bbnst below
#endif
1:	bbnst   r32, 2f
	IRQ_DISABLE(r20,r21)
	TRACE_IRQS_OFF
	movei   r0, 1
	mtspr   INTERRUPT_CRITICAL_SECTION, r0
	bzt     r30, .Lrestore_regs
	j       3f
2:	TRACE_IRQS_ON
	movei   r0, 1
	mtspr   INTERRUPT_CRITICAL_SECTION, r0
	IRQ_ENABLE(r20, r21)
	bzt     r30, .Lrestore_regs
3:


	/*
	 * We now commit to returning from this interrupt, since we will be
	 * doing things like setting EX_CONTEXT SPRs and unwinding the stack
	 * frame.  No calls should be made to any other code after this point.
	 * This code should only be entered with ICS set.
	 * r32 must still be set to ptregs.flags.
	 * We launch loads to each cache line separately first, so we can
	 * get some parallelism out of the memory subsystem.
	 * We start zeroing caller-saved registers throughout, since
	 * that will save some cycles if this turns out to be a syscall.
	 */
.Lrestore_regs:
	FEEDBACK_REENTER(interrupt_return)   /* called from elsewhere */

	/*
	 * Rotate so we have one high bit and one low bit to test.
	 * - low bit says whether to restore all the callee-saved registers,
	 *   or just r30-r33, and r52 up.
	 * - high bit (i.e. sign bit) says whether to restore all the
	 *   caller-saved registers, or just r0.
	 */
#if PT_FLAGS_CALLER_SAVES != 2 || PT_FLAGS_RESTORE_REGS != 4
# error Rotate trick does not work :-)
#endif
	{
	 rli    r20, r32, 30
	 PTREGS_PTR(sp, PTREGS_OFFSET_REG(0))
	}

	/*
	 * Load cache lines 0, 2, and 3 in that order, then use
	 * the last loaded value, which makes it likely that the other
	 * cache lines have also loaded, at which point we should be
	 * able to safely read all the remaining words on those cache
	 * lines without waiting for the memory subsystem.
	 */
	pop_reg_zero r0, r28, sp, PTREGS_OFFSET_REG(30) - PTREGS_OFFSET_REG(0)
	pop_reg_zero r30, r2, sp, PTREGS_OFFSET_PC - PTREGS_OFFSET_REG(30)
	pop_reg_zero r21, r3, sp, PTREGS_OFFSET_EX1 - PTREGS_OFFSET_PC
	pop_reg_zero lr, r4, sp, PTREGS_OFFSET_REG(52) - PTREGS_OFFSET_EX1
	{
	 mtspr  SPR_EX_CONTEXT_K_0, r21
	 move   r5, zero
	}
	{
	 mtspr  SPR_EX_CONTEXT_K_1, lr
	 andi   lr, lr, SPR_EX_CONTEXT_1_1__PL_MASK  /* mask off ICS */
	}

	/* Restore callee-saveds that we actually use. */
	pop_reg_zero r52, r6, sp, PTREGS_OFFSET_REG(31) - PTREGS_OFFSET_REG(52)
	pop_reg_zero r31, r7
	pop_reg_zero r32, r8
	pop_reg_zero r33, r9, sp, PTREGS_OFFSET_REG(29) - PTREGS_OFFSET_REG(33)

	/*
	 * If we modified other callee-saveds, restore them now.
	 * This is rare, but could be via ptrace or signal handler.
	 */
	{
	 move   r10, zero
	 bbs    r20, .Lrestore_callees
	}
.Lcontinue_restore_regs:

	/* Check if we're returning from a syscall. */
	{
	 move   r11, zero
	 blzt   r20, 1f  /* no, so go restore callee-save registers */
	}

	/*
	 * Check if we're returning to userspace.
	 * Note that if we're not, we don't worry about zeroing everything.
	 */
	{
	 addli  sp, sp, PTREGS_OFFSET_LR - PTREGS_OFFSET_REG(29)
	 bnz    lr, .Lkernel_return
	}

	/*
	 * On return from syscall, we've restored r0 from pt_regs, but we
	 * clear the remainder of the caller-saved registers.  We could
	 * restore the syscall arguments, but there's not much point,
	 * and it ensures user programs aren't trying to use the
	 * caller-saves if we clear them, as well as avoiding leaking
	 * kernel pointers into userspace.
	 */
	pop_reg_zero lr, r12, sp, PTREGS_OFFSET_TP - PTREGS_OFFSET_LR
	pop_reg_zero tp, r13, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_TP
	{
	 lw     sp, sp
	 move   r14, zero
	 move   r15, zero
	}
	{ move r16, zero; move r17, zero }
	{ move r18, zero; move r19, zero }
	{ move r20, zero; move r21, zero }
	{ move r22, zero; move r23, zero }
	{ move r24, zero; move r25, zero }
	{ move r26, zero; move r27, zero }

	/* Set r1 to errno if we are returning an error, otherwise zero. */
	{
	 moveli r29, 4096
	 sub    r1, zero, r0
	}
	slt_u   r29, r1, r29
	{
	 mnz    r1, r29, r1
	 move   r29, zero
	}
	iret

	/*
	 * Not a syscall, so restore caller-saved registers.
	 * First kick off a load for cache line 1, which we're touching
	 * for the first time here.
	 */
	.align 64
1:	pop_reg r29, sp, PTREGS_OFFSET_REG(1) - PTREGS_OFFSET_REG(29)
	pop_reg r1
	pop_reg r2
	pop_reg r3
	pop_reg r4
	pop_reg r5
	pop_reg r6
	pop_reg r7
	pop_reg r8
	pop_reg r9
	pop_reg r10
	pop_reg r11
	pop_reg r12
	pop_reg r13
	pop_reg r14
	pop_reg r15
	pop_reg r16
	pop_reg r17
	pop_reg r18
	pop_reg r19
	pop_reg r20
	pop_reg r21
	pop_reg r22
	pop_reg r23
	pop_reg r24
	pop_reg r25
	pop_reg r26
	pop_reg r27
	pop_reg r28, sp, PTREGS_OFFSET_LR - PTREGS_OFFSET_REG(28)
	/* r29 already restored above */
	bnz     lr, .Lkernel_return
	pop_reg lr, sp, PTREGS_OFFSET_TP - PTREGS_OFFSET_LR
	pop_reg tp, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_TP
	lw      sp, sp
	iret

	/*
	 * We can't restore tp when in kernel mode, since a thread might
	 * have migrated from another cpu and brought a stale tp value.
	 */
.Lkernel_return:
	pop_reg lr, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_LR
	lw      sp, sp
	iret

	/* Restore callee-saved registers from r34 to r51. */
.Lrestore_callees:
	addli  sp, sp, PTREGS_OFFSET_REG(34) - PTREGS_OFFSET_REG(29)
	pop_reg r34
	pop_reg r35
	pop_reg r36
	pop_reg r37
	pop_reg r38
	pop_reg r39
	pop_reg r40
	pop_reg r41
	pop_reg r42
	pop_reg r43
	pop_reg r44
	pop_reg r45
	pop_reg r46
	pop_reg r47
	pop_reg r48
	pop_reg r49
	pop_reg r50
	pop_reg r51, sp, PTREGS_OFFSET_REG(29) - PTREGS_OFFSET_REG(51)
	j .Lcontinue_restore_regs

.Lwork_pending:
	/* Mask the reschedule flag */
	andi    r28, r29, _TIF_NEED_RESCHED

	{
	 /*
	  * If the NEED_RESCHED flag is called, we call schedule(), which
	  * may drop this context right here and go do something else.
	  * On return, jump back to .Lresume_userspace and recheck.
	  */
	 bz     r28, .Lasync_tlb

	 /* Mask the async-tlb flag */
	 andi   r28, r29, _TIF_ASYNC_TLB
	}

	jal     schedule
	FEEDBACK_REENTER(interrupt_return)

	/* Reload the flags and check again */
	j       .Lresume_userspace

.Lasync_tlb:
	{
	 bz     r28, .Lneed_sigpending

	 /* Mask the sigpending flag */
	 andi   r28, r29, _TIF_SIGPENDING
	}

	PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	jal     do_async_page_fault
	FEEDBACK_REENTER(interrupt_return)

	/*
	 * Go restart the "resume userspace" process.  We may have
	 * fired a signal, and we need to disable interrupts again.
	 */
	j       .Lresume_userspace

.Lneed_sigpending:
	/*
	 * At this point we are either doing signal handling or single-step,
	 * so either way make sure we have all the registers saved.
	 */
	push_extra_callee_saves r0

	{
	 /* If no signal pending, skip to singlestep check */
	 bz     r28, .Lneed_singlestep

	 /* Mask the singlestep flag */
	 andi   r28, r29, _TIF_SINGLESTEP
	}

	jal     do_signal
	FEEDBACK_REENTER(interrupt_return)

	/* Reload the flags and check again */
	j       .Lresume_userspace

.Lneed_singlestep:
	{
	 /* Get a pointer to the EX1 field */
	 PTREGS_PTR(r29, PTREGS_OFFSET_EX1)

	 /* If we get here, our bit must be set. */
	 bz     r28, .Lwork_confusion
	}
	/* If we are in priv mode, don't single step */
	lw      r28, r29
	andi    r28, r28, SPR_EX_CONTEXT_1_1__PL_MASK  /* mask off ICS */
	bnz     r28, .Lrestore_all

	/* Allow interrupts within the single step code */
	TRACE_IRQS_ON  /* Note: clobbers registers r0-r29 */
	IRQ_ENABLE(r20, r21)

	/* try to single-step the current instruction */
	PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	jal     single_step_once
	FEEDBACK_REENTER(interrupt_return)

	/* Re-disable interrupts.  TRACE_IRQS_OFF in .Lrestore_all. */
	IRQ_DISABLE(r20,r21)

	j       .Lrestore_all

.Lwork_confusion:
	move    r0, r28
	panic   "thread_info allwork flags unhandled on userspace resume: %#x"

	STD_ENDPROC(interrupt_return)

	/*
	 * Some interrupts don't check for single stepping
	 */
	.pushsection .text.handle_interrupt_no_single_step,"ax"
handle_interrupt_no_single_step:
	finish_interrupt_save handle_interrupt_no_single_step
	{
	 jalr   r0
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	}
	FEEDBACK_REENTER(handle_interrupt_no_single_step)
	{
	 movei  r30, 0   /* not an NMI */
	 j      interrupt_return
	}
	STD_ENDPROC(handle_interrupt_no_single_step)

	/*
	 * "NMI" interrupts mask ALL interrupts before calling the
	 * handler, and don't check thread flags, etc., on the way
	 * back out.  In general, the only things we do here for NMIs
	 * are the register save/restore, fixing the PC if we were
	 * doing single step, and the dataplane kernel-TLB management.
	 * We don't (for example) deal with start/stop of the sched tick.
	 */
	.pushsection .text.handle_nmi,"ax"
handle_nmi:
	finish_interrupt_save handle_nmi
	check_single_stepping normal, .Ldispatch_nmi
.Ldispatch_nmi:
	{
	 jalr   r0
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	}
	FEEDBACK_REENTER(handle_nmi)
	j       interrupt_return
	STD_ENDPROC(handle_nmi)

	/*
	 * Parallel code for syscalls to handle_interrupt.
	 */
	.pushsection .text.handle_syscall,"ax"
handle_syscall:
	finish_interrupt_save handle_syscall

	/*
	 * Check for if we are single stepping in user level. If so, then
	 * we need to restore the PC.
	 */
	check_single_stepping syscall, .Ldispatch_syscall
.Ldispatch_syscall:

	/* Enable irqs. */
	TRACE_IRQS_ON
	IRQ_ENABLE(r20, r21)

	/* Bump the counter for syscalls made on this tile. */
	moveli  r20, lo16(irq_stat + IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET)
	auli    r20, r20, ha16(irq_stat + IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET)
	add     r20, r20, tp
	lw      r21, r20
	addi    r21, r21, 1
	sw      r20, r21

	/* Trace syscalls, if requested. */
	GET_THREAD_INFO(r31)
	addi	r31, r31, THREAD_INFO_FLAGS_OFFSET
	lw	r30, r31
	andi    r30, r30, _TIF_SYSCALL_TRACE
	bzt	r30, .Lrestore_syscall_regs
	jal	do_syscall_trace
	FEEDBACK_REENTER(handle_syscall)

	/*
	 * We always reload our registers from the stack at this
	 * point.  They might be valid, if we didn't build with
	 * TRACE_IRQFLAGS, and this isn't a dataplane tile, and we're not
	 * doing syscall tracing, but there are enough cases now that it
	 * seems simplest just to do the reload unconditionally.
	 */
.Lrestore_syscall_regs:
	PTREGS_PTR(r11, PTREGS_OFFSET_REG(0))
	pop_reg r0, r11
	pop_reg r1, r11
	pop_reg r2, r11
	pop_reg r3, r11
	pop_reg r4, r11
	pop_reg r5, r11, PTREGS_OFFSET_SYSCALL - PTREGS_OFFSET_REG(5)
	pop_reg TREG_SYSCALL_NR_NAME, r11

	/* Ensure that the syscall number is within the legal range. */
	moveli  r21, __NR_syscalls
	{
	 slt_u  r21, TREG_SYSCALL_NR_NAME, r21
	 moveli r20, lo16(sys_call_table)
	}
	{
	 bbns   r21, .Linvalid_syscall
	 auli   r20, r20, ha16(sys_call_table)
	}
	s2a     r20, TREG_SYSCALL_NR_NAME, r20
	lw      r20, r20

	/* Jump to syscall handler. */
	jalr    r20
.Lhandle_syscall_link: /* value of "lr" after "jalr r20" above */

	/*
	 * Write our r0 onto the stack so it gets restored instead
	 * of whatever the user had there before.
	 */
	PTREGS_PTR(r29, PTREGS_OFFSET_REG(0))
	sw      r29, r0

.Lsyscall_sigreturn_skip:
	FEEDBACK_REENTER(handle_syscall)

	/* Do syscall trace again, if requested. */
	lw	r30, r31
	andi    r30, r30, _TIF_SYSCALL_TRACE
	bzt     r30, 1f
	jal	do_syscall_trace
	FEEDBACK_REENTER(handle_syscall)
1:	j       .Lresume_userspace   /* jump into middle of interrupt_return */

.Linvalid_syscall:
	/* Report an invalid syscall back to the user program */
	{
	 PTREGS_PTR(r29, PTREGS_OFFSET_REG(0))
	 movei  r28, -ENOSYS
	}
	sw      r29, r28
	j       .Lresume_userspace   /* jump into middle of interrupt_return */
	STD_ENDPROC(handle_syscall)

	/* Return the address for oprofile to suppress in backtraces. */
STD_ENTRY_SECTION(handle_syscall_link_address, .text.handle_syscall)
	lnk     r0
	{
	 addli  r0, r0, .Lhandle_syscall_link - .
	 jrp    lr
	}
	STD_ENDPROC(handle_syscall_link_address)

STD_ENTRY(ret_from_fork)
	jal     sim_notify_fork
	jal     schedule_tail
	FEEDBACK_REENTER(ret_from_fork)
	j       .Lresume_userspace   /* jump into middle of interrupt_return */
	STD_ENDPROC(ret_from_fork)

	/*
	 * Code for ill interrupt.
	 */
	.pushsection .text.handle_ill,"ax"
handle_ill:
	finish_interrupt_save handle_ill

	/*
	 * Check for if we are single stepping in user level. If so, then
	 * we need to restore the PC.
	 */
	check_single_stepping ill, .Ldispatch_normal_ill

	{
	 /* See if the PC is the 1st bundle in the buffer */
	 seq    r25, r27, r26

	 /* Point to the 2nd bundle in the buffer */
	 addi   r26, r26, 8
	}
	{
	 /* Point to the original pc */
	 addi   r24, r29, SINGLESTEP_STATE_ORIG_PC_OFFSET

	 /* Branch if the PC is the 1st bundle in the buffer */
	 bnz    r25, 3f
	}
	{
	 /* See if the PC is the 2nd bundle of the buffer */
	 seq    r25, r27, r26

	 /* Set PC to next instruction */
	 addi   r24, r29, SINGLESTEP_STATE_NEXT_PC_OFFSET
	}
	{
	 /* Point to flags */
	 addi   r25, r29, SINGLESTEP_STATE_FLAGS_OFFSET

	 /* Branch if PC is in the second bundle */
	 bz     r25, 2f
	}
	/* Load flags */
	lw      r25, r25
	{
	 /*
	  * Get the offset for the register to restore
	  * Note: the lower bound is 2, so we have implicit scaling by 4.
	  *  No multiplication of the register number by the size of a register
	  *  is needed.
	  */
	 mm     r27, r25, zero, SINGLESTEP_STATE_TARGET_LB, \
		SINGLESTEP_STATE_TARGET_UB

	 /* Mask Rewrite_LR */
	 andi   r25, r25, SINGLESTEP_STATE_MASK_UPDATE
	}
	{
	 addi   r29, r29, SINGLESTEP_STATE_UPDATE_VALUE_OFFSET

	 /* Don't rewrite temp register */
	 bz     r25, 3f
	}
	{
	 /* Get the temp value */
	 lw     r29, r29

	 /* Point to where the register is stored */
	 add    r27, r27, sp
	}

	/* Add in the C ABI save area size to the register offset */
	addi    r27, r27, C_ABI_SAVE_AREA_SIZE

	/* Restore the user's register with the temp value */
	sw      r27, r29
	j       3f

2:
	/* Must be in the third bundle */
	addi    r24, r29, SINGLESTEP_STATE_BRANCH_NEXT_PC_OFFSET

3:
	/* set PC and continue */
	lw      r26, r24
	sw      r28, r26

	/*
	 * Clear TIF_SINGLESTEP to prevent recursion if we execute an ill.
	 * The normal non-arch flow redundantly clears TIF_SINGLESTEP, but we
	 * need to clear it here and can't really impose on all other arches.
	 * So what's another write between friends?
	 */
	GET_THREAD_INFO(r0)

	addi    r1, r0, THREAD_INFO_FLAGS_OFFSET
	{
	 lw     r2, r1
	 addi   r0, r0, THREAD_INFO_TASK_OFFSET  /* currently a no-op */
	}
	andi    r2, r2, ~_TIF_SINGLESTEP
	sw      r1, r2

	/* Issue a sigtrap */
	{
	 lw     r0, r0          /* indirect thru thread_info to get task_info*/
	 addi   r1, sp, C_ABI_SAVE_AREA_SIZE  /* put ptregs pointer into r1 */
	 move   r2, zero        /* load error code into r2 */
	}

	jal     send_sigtrap    /* issue a SIGTRAP */
	FEEDBACK_REENTER(handle_ill)
	j       .Lresume_userspace   /* jump into middle of interrupt_return */

.Ldispatch_normal_ill:
	{
	 jalr   r0
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	}
	FEEDBACK_REENTER(handle_ill)
	{
	 movei  r30, 0   /* not an NMI */
	 j      interrupt_return
	}
	STD_ENDPROC(handle_ill)

/* Various stub interrupt handlers and syscall handlers */

STD_ENTRY_LOCAL(_kernel_double_fault)
	mfspr   r1, SPR_EX_CONTEXT_K_0
	move    r2, lr
	move    r3, sp
	move    r4, r52
	addi    sp, sp, -C_ABI_SAVE_AREA_SIZE
	j       kernel_double_fault
	STD_ENDPROC(_kernel_double_fault)

STD_ENTRY_LOCAL(bad_intr)
	mfspr   r2, SPR_EX_CONTEXT_K_0
	panic   "Unhandled interrupt %#x: PC %#lx"
	STD_ENDPROC(bad_intr)

/* Put address of pt_regs in reg and jump. */
#define PTREGS_SYSCALL(x, reg)                          \
	STD_ENTRY(_##x);                                \
	{                                               \
	 PTREGS_PTR(reg, PTREGS_OFFSET_BASE);           \
	 j      x                                       \
	};                                              \
	STD_ENDPROC(_##x)

/*
 * Special-case sigreturn to not write r0 to the stack on return.
 * This is technically more efficient, but it also avoids difficulties
 * in the 64-bit OS when handling 32-bit compat code, since we must not
 * sign-extend r0 for the sigreturn return-value case.
 */
#define PTREGS_SYSCALL_SIGRETURN(x, reg)                \
	STD_ENTRY(_##x);                                \
	addli   lr, lr, .Lsyscall_sigreturn_skip - .Lhandle_syscall_link; \
	{                                               \
	 PTREGS_PTR(reg, PTREGS_OFFSET_BASE);           \
	 j      x                                       \
	};                                              \
	STD_ENDPROC(_##x)

PTREGS_SYSCALL(sys_execve, r3)
PTREGS_SYSCALL(sys_sigaltstack, r2)
PTREGS_SYSCALL_SIGRETURN(sys_rt_sigreturn, r0)
PTREGS_SYSCALL(sys_cmpxchg_badaddr, r1)

/* Save additional callee-saves to pt_regs, put address in r4 and jump. */
STD_ENTRY(_sys_clone)
	push_extra_callee_saves r4
	j       sys_clone
	STD_ENDPROC(_sys_clone)

/*
 * This entrypoint is taken for the cmpxchg and atomic_update fast
 * swints.  We may wish to generalize it to other fast swints at some
 * point, but for now there are just two very similar ones, which
 * makes it faster.
 *
 * The fast swint code is designed to have a small footprint.  It does
 * not save or restore any GPRs, counting on the caller-save registers
 * to be available to it on entry.  It does not modify any callee-save
 * registers (including "lr").  It does not check what PL it is being
 * called at, so you'd better not call it other than at PL0.
 * The <atomic.h> wrapper assumes it only clobbers r20-r29, so if
 * it ever is necessary to use more registers, be aware.
 *
 * It does not use the stack, but since it might be re-interrupted by
 * a page fault which would assume the stack was valid, it does
 * save/restore the stack pointer and zero it out to make sure it gets reset.
 * Since we always keep interrupts disabled, the hypervisor won't
 * clobber our EX_CONTEXT_K_x registers, so we don't save/restore them
 * (other than to advance the PC on return).
 *
 * We have to manually validate the user vs kernel address range
 * (since at PL1 we can read/write both), and for performance reasons
 * we don't allow cmpxchg on the fc000000 memory region, since we only
 * validate that the user address is below PAGE_OFFSET.
 *
 * We place it in the __HEAD section to ensure it is relatively
 * near to the intvec_SWINT_1 code (reachable by a conditional branch).
 *
 * Must match register usage in do_page_fault().
 */
	__HEAD
	.align 64
	/* Align much later jump on the start of a cache line. */
#if !ATOMIC_LOCKS_FOUND_VIA_TABLE()
	nop
#if PAGE_SIZE >= 0x10000
	nop
#endif
#endif
ENTRY(sys_cmpxchg)

	/*
	 * Save "sp" and set it zero for any possible page fault.
	 *
	 * HACK: We want to both zero sp and check r0's alignment,
	 * so we do both at once. If "sp" becomes nonzero we
	 * know r0 is unaligned and branch to the error handler that
	 * restores sp, so this is OK.
	 *
	 * ICS is disabled right now so having a garbage but nonzero
	 * sp is OK, since we won't execute any faulting instructions
	 * when it is nonzero.
	 */
	{
	 move   r27, sp
	 andi	sp, r0, 3
	}

	/*
	 * Get the lock address in ATOMIC_LOCK_REG, and also validate that the
	 * address is less than PAGE_OFFSET, since that won't trap at PL1.
	 * We only use bits less than PAGE_SHIFT to avoid having to worry
	 * about aliasing among multiple mappings of the same physical page,
	 * and we ignore the low 3 bits so we have one lock that covers
	 * both a cmpxchg64() and a cmpxchg() on either its low or high word.
	 * NOTE: this must match __atomic_hashed_lock() in lib/atomic_32.c.
	 */

#if (PAGE_OFFSET & 0xffff) != 0
# error Code here assumes PAGE_OFFSET can be loaded with just hi16()
#endif

#if ATOMIC_LOCKS_FOUND_VIA_TABLE()
	{
	 /* Check for unaligned input. */
	 bnz    sp, .Lcmpxchg_badaddr
	 mm     r25, r0, zero, 3, PAGE_SHIFT-1
	}
	{
	 crc32_32 r25, zero, r25
	 moveli r21, lo16(atomic_lock_ptr)
	}
	{
	 auli   r21, r21, ha16(atomic_lock_ptr)
	 auli   r23, zero, hi16(PAGE_OFFSET)  /* hugepage-aligned */
	}
	{
	 shri	r20, r25, 32 - ATOMIC_HASH_L1_SHIFT
	 slt_u  r23, r0, r23

	 /*
	  * Ensure that the TLB is loaded before we take out the lock.
	  * On TILEPro, this will start fetching the value all the way
	  * into our L1 as well (and if it gets modified before we
	  * grab the lock, it will be invalidated from our cache
	  * before we reload it).  On tile64, we'll start fetching it
	  * into our L1 if we're the home, and if we're not, we'll
	  * still at least start fetching it into the home's L2.
	  */
	 lw	r26, r0
	}
	{
	 s2a    r21, r20, r21
	 bbns   r23, .Lcmpxchg_badaddr
	}
	{
	 lw     r21, r21
	 seqi	r23, TREG_SYSCALL_NR_NAME, __NR_FAST_cmpxchg64
	 andi	r25, r25, ATOMIC_HASH_L2_SIZE - 1
	}
	{
	 /* Branch away at this point if we're doing a 64-bit cmpxchg. */
	 bbs    r23, .Lcmpxchg64
	 andi   r23, r0, 7       /* Precompute alignment for cmpxchg64. */
	}

	{
	 /*
	  * We very carefully align the code that actually runs with
	  * the lock held (nine bundles) so that we know it is all in
	  * the icache when we start.  This instruction (the jump) is
	  * at the start of the first cache line, address zero mod 64;
	  * we jump to somewhere in the second cache line to issue the
	  * tns, then jump back to finish up.
	  */
	 s2a	ATOMIC_LOCK_REG_NAME, r25, r21
	 j      .Lcmpxchg32_tns
	}

#else /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */
	{
	 /* Check for unaligned input. */
	 bnz    sp, .Lcmpxchg_badaddr
	 auli   r23, zero, hi16(PAGE_OFFSET)  /* hugepage-aligned */
	}
	{
	 /*
	  * Slide bits into position for 'mm'. We want to ignore
	  * the low 3 bits of r0, and consider only the next
	  * ATOMIC_HASH_SHIFT bits.
	  * Because of C pointer arithmetic, we want to compute this:
	  *
	  * ((char*)atomic_locks +
	  *  (((r0 >> 3) & (1 << (ATOMIC_HASH_SIZE - 1))) << 2))
	  *
	  * Instead of two shifts we just ">> 1", and use 'mm'
	  * to ignore the low and high bits we don't want.
	  */
	 shri	r25, r0, 1

	 slt_u  r23, r0, r23

	 /*
	  * Ensure that the TLB is loaded before we take out the lock.
	  * On tilepro, this will start fetching the value all the way
	  * into our L1 as well (and if it gets modified before we
	  * grab the lock, it will be invalidated from our cache
	  * before we reload it).  On tile64, we'll start fetching it
	  * into our L1 if we're the home, and if we're not, we'll
	  * still at least start fetching it into the home's L2.
	  */
	 lw	r26, r0
	}
	{
	 auli	r21, zero, ha16(atomic_locks)

	 bbns   r23, .Lcmpxchg_badaddr
	}
#if PAGE_SIZE < 0x10000
	/* atomic_locks is page-aligned so for big pages we don't need this. */
	addli   r21, r21, lo16(atomic_locks)
#endif
	{
	 /*
	  * Insert the hash bits into the page-aligned pointer.
	  * ATOMIC_HASH_SHIFT is so big that we don't actually hash
	  * the unmasked address bits, as that may cause unnecessary
	  * collisions.
	  */
	 mm	ATOMIC_LOCK_REG_NAME, r25, r21, 2, (ATOMIC_HASH_SHIFT + 2) - 1

	 seqi	r23, TREG_SYSCALL_NR_NAME, __NR_FAST_cmpxchg64
	}
	{
	 /* Branch away at this point if we're doing a 64-bit cmpxchg. */
	 bbs    r23, .Lcmpxchg64
	 andi   r23, r0, 7       /* Precompute alignment for cmpxchg64. */
	}
	{
	 /*
	  * We very carefully align the code that actually runs with
	  * the lock held (nine bundles) so that we know it is all in
	  * the icache when we start.  This instruction (the jump) is
	  * at the start of the first cache line, address zero mod 64;
	  * we jump to somewhere in the second cache line to issue the
	  * tns, then jump back to finish up.
	  */
	 j      .Lcmpxchg32_tns
	}

#endif /* ATOMIC_LOCKS_FOUND_VIA_TABLE() */

	ENTRY(__sys_cmpxchg_grab_lock)

	/*
	 * Perform the actual cmpxchg or atomic_update.
	 * Note that the system <arch/atomic.h> header relies on
	 * atomic_update() to always perform an "mf", so don't make
	 * it optional or conditional without modifying that code.
	 */
.Ldo_cmpxchg32:
	{
	 lw     r21, r0
	 seqi	r23, TREG_SYSCALL_NR_NAME, __NR_FAST_atomic_update
	 move	r24, r2
	}
	{
	 seq    r22, r21, r1     /* See if cmpxchg matches. */
	 and	r25, r21, r1     /* If atomic_update, compute (*mem & mask) */
	}
	{
	 or	r22, r22, r23    /* Skip compare branch for atomic_update. */
	 add	r25, r25, r2     /* Compute (*mem & mask) + addend. */
	}
	{
	 mvnz	r24, r23, r25    /* Use atomic_update value if appropriate. */
	 bbns   r22, .Lcmpxchg32_mismatch
	}
	sw      r0, r24

	/* Do slow mtspr here so the following "mf" waits less. */
	{
	 move   sp, r27
	 mtspr  SPR_EX_CONTEXT_K_0, r28
	}
	mf

	/* The following instruction is the start of the second cache line. */
	{
	 move   r0, r21
	 sw     ATOMIC_LOCK_REG_NAME, zero
	}
	iret

	/* Duplicated code here in the case where we don't overlap "mf" */
.Lcmpxchg32_mismatch:
	{
	 move   r0, r21
	 sw     ATOMIC_LOCK_REG_NAME, zero
	}
	{
	 move   sp, r27
	 mtspr  SPR_EX_CONTEXT_K_0, r28
	}
	iret

	/*
	 * The locking code is the same for 32-bit cmpxchg/atomic_update,
	 * and for 64-bit cmpxchg.  We provide it as a macro and put
	 * it into both versions.  We can't share the code literally
	 * since it depends on having the right branch-back address.
	 * Note that the first few instructions should share the cache
	 * line with the second half of the actual locked code.
	 */
	.macro  cmpxchg_lock, bitwidth

	/* Lock; if we succeed, jump back up to the read-modify-write. */
#ifdef CONFIG_SMP
	tns     r21, ATOMIC_LOCK_REG_NAME
#else
	/*
	 * Non-SMP preserves all the lock infrastructure, to keep the
	 * code simpler for the interesting (SMP) case.  However, we do
	 * one small optimization here and in atomic_asm.S, which is
	 * to fake out acquiring the actual lock in the atomic_lock table.
	 */
	movei	r21, 0
#endif

	/* Issue the slow SPR here while the tns result is in flight. */
	mfspr   r28, SPR_EX_CONTEXT_K_0

	{
	 addi   r28, r28, 8    /* return to the instruction after the swint1 */
	 bzt    r21, .Ldo_cmpxchg\bitwidth
	}
	/*
	 * The preceding instruction is the last thing that must be
	 * on the second cache line.
	 */

#ifdef CONFIG_SMP
	/*
	 * We failed to acquire the tns lock on our first try.  Now use
	 * bounded exponential backoff to retry, like __atomic_spinlock().
	 */
	{
	 moveli r23, 2048       /* maximum backoff time in cycles */
	 moveli r25, 32         /* starting backoff time in cycles */
	}
1:	mfspr   r26, CYCLE_LOW  /* get start point for this backoff */
2:	mfspr   r22, CYCLE_LOW  /* test to see if we've backed off enough */
	sub     r22, r22, r26
	slt     r22, r22, r25
	bbst    r22, 2b
	{
	 shli   r25, r25, 1     /* double the backoff; retry the tns */
	 tns    r21, ATOMIC_LOCK_REG_NAME
	}
	slt     r26, r23, r25   /* is the proposed backoff too big? */
	{
	 mvnz   r25, r26, r23
	 bzt    r21, .Ldo_cmpxchg\bitwidth
	}
	j       1b
#endif /* CONFIG_SMP */
	.endm

.Lcmpxchg32_tns:
	cmpxchg_lock 32

	/*
	 * This code is invoked from sys_cmpxchg after most of the
	 * preconditions have been checked.  We still need to check
	 * that r0 is 8-byte aligned, since if it's not we won't
	 * actually be atomic.  However, ATOMIC_LOCK_REG has the atomic
	 * lock pointer and r27/r28 have the saved SP/PC.
	 * r23 is holding "r0 & 7" so we can test for alignment.
	 * The compare value is in r2/r3; the new value is in r4/r5.
	 * On return, we must put the old value in r0/r1.
	 */
	.align 64
.Lcmpxchg64:
	{
#if ATOMIC_LOCKS_FOUND_VIA_TABLE()
	 s2a	ATOMIC_LOCK_REG_NAME, r25, r21
#endif
	 bzt     r23, .Lcmpxchg64_tns
	}
	j       .Lcmpxchg_badaddr

.Ldo_cmpxchg64:
	{
	 lw     r21, r0
	 addi   r25, r0, 4
	}
	{
	 lw     r1, r25
	}
	seq     r26, r21, r2
	{
	 bz     r26, .Lcmpxchg64_mismatch
	 seq    r26, r1, r3
	}
	{
	 bz     r26, .Lcmpxchg64_mismatch
	}
	sw      r0, r4
	sw      r25, r5

	/*
	 * The 32-bit path provides optimized "match" and "mismatch"
	 * iret paths, but we don't have enough bundles in this cache line
	 * to do that, so we just make even the "mismatch" path do an "mf".
	 */
.Lcmpxchg64_mismatch:
	{
	 move   sp, r27
	 mtspr  SPR_EX_CONTEXT_K_0, r28
	}
	mf
	{
	 move   r0, r21
	 sw     ATOMIC_LOCK_REG_NAME, zero
	}
	iret

.Lcmpxchg64_tns:
	cmpxchg_lock 64


	/*
	 * Reset sp and revector to sys_cmpxchg_badaddr(), which will
	 * just raise the appropriate signal and exit.  Doing it this
	 * way means we don't have to duplicate the code in intvec.S's
	 * int_hand macro that locates the top of the stack.
	 */
.Lcmpxchg_badaddr:
	{
	 moveli TREG_SYSCALL_NR_NAME, __NR_cmpxchg_badaddr
	 move   sp, r27
	}
	j       intvec_SWINT_1
	ENDPROC(sys_cmpxchg)
	ENTRY(__sys_cmpxchg_end)


/* The single-step support may need to read all the registers. */
int_unalign:
	push_extra_callee_saves r0
	j       do_trap

/* Include .intrpt1 array of interrupt vectors */
	.section ".intrpt1", "ax"

#define op_handle_perf_interrupt bad_intr
#define op_handle_aux_perf_interrupt bad_intr

#ifndef CONFIG_HARDWALL
#define do_hardwall_trap bad_intr
#endif

	int_hand     INT_ITLB_MISS, ITLB_MISS, \
		     do_page_fault, handle_interrupt_no_single_step
	int_hand     INT_MEM_ERROR, MEM_ERROR, bad_intr
	int_hand     INT_ILL, ILL, do_trap, handle_ill
	int_hand     INT_GPV, GPV, do_trap
	int_hand     INT_SN_ACCESS, SN_ACCESS, do_trap
	int_hand     INT_IDN_ACCESS, IDN_ACCESS, do_trap
	int_hand     INT_UDN_ACCESS, UDN_ACCESS, do_trap
	int_hand     INT_IDN_REFILL, IDN_REFILL, bad_intr
	int_hand     INT_UDN_REFILL, UDN_REFILL, bad_intr
	int_hand     INT_IDN_COMPLETE, IDN_COMPLETE, bad_intr
	int_hand     INT_UDN_COMPLETE, UDN_COMPLETE, bad_intr
	int_hand     INT_SWINT_3, SWINT_3, do_trap
	int_hand     INT_SWINT_2, SWINT_2, do_trap
	int_hand     INT_SWINT_1, SWINT_1, SYSCALL, handle_syscall
	int_hand     INT_SWINT_0, SWINT_0, do_trap
	int_hand     INT_UNALIGN_DATA, UNALIGN_DATA, int_unalign
	int_hand     INT_DTLB_MISS, DTLB_MISS, do_page_fault
	int_hand     INT_DTLB_ACCESS, DTLB_ACCESS, do_page_fault
	int_hand     INT_DMATLB_MISS, DMATLB_MISS, do_page_fault
	int_hand     INT_DMATLB_ACCESS, DMATLB_ACCESS, do_page_fault
	int_hand     INT_SNITLB_MISS, SNITLB_MISS, do_page_fault
	int_hand     INT_SN_NOTIFY, SN_NOTIFY, bad_intr
	int_hand     INT_SN_FIREWALL, SN_FIREWALL, do_hardwall_trap
	int_hand     INT_IDN_FIREWALL, IDN_FIREWALL, bad_intr
	int_hand     INT_UDN_FIREWALL, UDN_FIREWALL, do_hardwall_trap
	int_hand     INT_TILE_TIMER, TILE_TIMER, do_timer_interrupt
	int_hand     INT_IDN_TIMER, IDN_TIMER, bad_intr
	int_hand     INT_UDN_TIMER, UDN_TIMER, bad_intr
	int_hand     INT_DMA_NOTIFY, DMA_NOTIFY, bad_intr
	int_hand     INT_IDN_CA, IDN_CA, bad_intr
	int_hand     INT_UDN_CA, UDN_CA, bad_intr
	int_hand     INT_IDN_AVAIL, IDN_AVAIL, bad_intr
	int_hand     INT_UDN_AVAIL, UDN_AVAIL, bad_intr
	int_hand     INT_PERF_COUNT, PERF_COUNT, \
		     op_handle_perf_interrupt, handle_nmi
	int_hand     INT_INTCTRL_3, INTCTRL_3, bad_intr
#if CONFIG_KERNEL_PL == 2
	dc_dispatch  INT_INTCTRL_2, INTCTRL_2
	int_hand     INT_INTCTRL_1, INTCTRL_1, bad_intr
#else
	int_hand     INT_INTCTRL_2, INTCTRL_2, bad_intr
	dc_dispatch  INT_INTCTRL_1, INTCTRL_1
#endif
	int_hand     INT_INTCTRL_0, INTCTRL_0, bad_intr
	int_hand     INT_MESSAGE_RCV_DWNCL, MESSAGE_RCV_DWNCL, \
		     hv_message_intr
	int_hand     INT_DEV_INTR_DWNCL, DEV_INTR_DWNCL, \
		     tile_dev_intr
	int_hand     INT_I_ASID, I_ASID, bad_intr
	int_hand     INT_D_ASID, D_ASID, bad_intr
	int_hand     INT_DMATLB_MISS_DWNCL, DMATLB_MISS_DWNCL, \
		     do_page_fault
	int_hand     INT_SNITLB_MISS_DWNCL, SNITLB_MISS_DWNCL, \
		     do_page_fault
	int_hand     INT_DMATLB_ACCESS_DWNCL, DMATLB_ACCESS_DWNCL, \
		     do_page_fault
	int_hand     INT_SN_CPL, SN_CPL, bad_intr
	int_hand     INT_DOUBLE_FAULT, DOUBLE_FAULT, do_trap
#if CHIP_HAS_AUX_PERF_COUNTERS()
	int_hand     INT_AUX_PERF_COUNT, AUX_PERF_COUNT, \
		     op_handle_aux_perf_interrupt, handle_nmi
#endif

	/* Synthetic interrupt delivered only by the simulator */
	int_hand     INT_BREAKPOINT, BREAKPOINT, do_breakpoint
