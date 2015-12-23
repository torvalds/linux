/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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
#include <linux/unistd.h>
#include <linux/init.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/asm-offsets.h>
#include <asm/types.h>
#include <asm/traps.h>
#include <asm/signal.h>
#include <hv/hypervisor.h>
#include <arch/abi.h>
#include <arch/interrupts.h>
#include <arch/spr_def.h>

#define PTREGS_PTR(reg, ptreg) addli reg, sp, C_ABI_SAVE_AREA_SIZE + (ptreg)

#define PTREGS_OFFSET_SYSCALL PTREGS_OFFSET_REG(TREG_SYSCALL_NR)

#if CONFIG_KERNEL_PL == 1 || CONFIG_KERNEL_PL == 2
/*
 * Set "result" non-zero if ex1 holds the PL of the kernel
 * (with or without ICS being set).  Note this works only
 * because we never find the PL at level 3.
 */
# define IS_KERNEL_EX1(result, ex1) andi result, ex1, CONFIG_KERNEL_PL
#else
# error Recode IS_KERNEL_EX1 for CONFIG_KERNEL_PL
#endif

	.macro  push_reg reg, ptr=sp, delta=-8
	{
	 st     \ptr, \reg
	 addli  \ptr, \ptr, \delta
	}
	.endm

	.macro  pop_reg reg, ptr=sp, delta=8
	{
	 ld     \reg, \ptr
	 addli  \ptr, \ptr, \delta
	}
	.endm

	.macro  pop_reg_zero reg, zreg, ptr=sp, delta=8
	{
	 move   \zreg, zero
	 ld     \reg, \ptr
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
	 moveli r0, hw2_last(1b)
	}
	{
	 shl16insli r0, r0, hw1(1b)
	}
	{
	 shl16insli r0, r0, hw0(1b)
	 jal    panic
	}
	.endm

	/*
	 * Unalign data exception fast handling: In order to handle
	 * unaligned data access, a fast JIT version is generated and stored
	 * in a specific area in user space. We first need to do a quick poke
	 * to see if the JIT is available. We use certain bits in the fault
	 * PC (3 to 9 is used for 16KB page size) as index to address the JIT
	 * code area. The first 64bit word is the fault PC, and the 2nd one is
	 * the fault bundle itself. If these 2 words both match, then we
	 * directly "iret" to JIT code. If not, a slow path is invoked to
	 * generate new JIT code. Note: the current JIT code WILL be
	 * overwritten if it existed. So, ideally we can handle 128 unalign
	 * fixups via JIT. For lookup efficiency and to effectively support
	 * tight loops with multiple unaligned reference, a simple
	 * direct-mapped cache is used.
	 *
	 * SPR_EX_CONTEXT_K_0 is modified to return to JIT code.
	 * SPR_EX_CONTEXT_K_1 has ICS set.
	 * SPR_EX_CONTEXT_0_0 is setup to user program's next PC.
	 * SPR_EX_CONTEXT_0_1 = 0.
	 */
	.macro int_hand_unalign_fast  vecnum, vecname
	.org  (\vecnum << 8)
intvec_\vecname:
	/* Put r3 in SPR_SYSTEM_SAVE_K_1.  */
	mtspr   SPR_SYSTEM_SAVE_K_1, r3

	mfspr   r3, SPR_EX_CONTEXT_K_1
	/*
	 * Examine if exception comes from user without ICS set.
	 * If not, just go directly to the slow path.
	 */
	bnez    r3, hand_unalign_slow_nonuser

	mfspr   r3, SPR_SYSTEM_SAVE_K_0

	/* Get &thread_info->unalign_jit_tmp[0] in r3. */
	bfexts  r3, r3, 0, CPU_SHIFT-1
	mm      r3, zero, LOG2_THREAD_SIZE, 63
	addli   r3, r3, THREAD_INFO_UNALIGN_JIT_TMP_OFFSET

	/*
	 * Save r0, r1, r2 into thread_info array r3 points to
	 * from low to high memory in order.
	 */
	st_add  r3, r0, 8
	st_add  r3, r1, 8
	{
	 st_add r3, r2, 8
	 andi   r2, sp, 7
	}

	/* Save stored r3 value so we can revert it on a page fault. */
	mfspr   r1, SPR_SYSTEM_SAVE_K_1
	st      r3, r1

	{
	 /* Generate a SIGBUS if sp is not 8-byte aligned. */
	 bnez   r2, hand_unalign_slow_badsp
	}

	/*
	 * Get the thread_info in r0; load r1 with pc. Set the low bit of sp
	 * as an indicator to the page fault code in case we fault.
	 */
	{
	 ori    sp, sp, 1
	 mfspr  r1, SPR_EX_CONTEXT_K_0
	}

	/* Add the jit_info offset in thread_info; extract r1 [3:9] into r2. */
	{
	 addli  r0, r3, THREAD_INFO_UNALIGN_JIT_BASE_OFFSET - \
	  (THREAD_INFO_UNALIGN_JIT_TMP_OFFSET + (3 * 8))
	 bfextu r2, r1, 3, (2 + PAGE_SHIFT - UNALIGN_JIT_SHIFT)
	}

	/* Load the jit_info; multiply r2 by 128. */
	{
	 ld     r0, r0
	 shli   r2, r2, UNALIGN_JIT_SHIFT
	}

	/*
	 * If r0 is NULL, the JIT page is not mapped, so go to slow path;
	 * add offset r2 to r0 at the same time.
	 */
	{
	 beqz   r0, hand_unalign_slow
	 add    r2, r0, r2
	}

        /*
	 * We are loading from userspace (both the JIT info PC and
	 * instruction word, and the instruction word we executed)
	 * and since either could fault while holding the interrupt
	 * critical section, we must tag this region and check it in
	 * do_page_fault() to handle it properly.
	 */
ENTRY(__start_unalign_asm_code)

	/* Load first word of JIT in r0 and increment r2 by 8. */
	ld_add  r0, r2, 8

	/*
	 * Compare the PC with the 1st word in JIT; load the fault bundle
	 * into r1.
	 */
	{
	 cmpeq  r0, r0, r1
	 ld     r1, r1
	}

	/* Go to slow path if PC doesn't match. */
	beqz    r0, hand_unalign_slow

	/*
	 * Load the 2nd word of JIT, which is supposed to be the fault
	 * bundle for a cache hit. Increment r2; after this bundle r2 will
	 * point to the potential start of the JIT code we want to run.
	 */
	ld_add  r0, r2, 8

	/* No further accesses to userspace are done after this point. */
ENTRY(__end_unalign_asm_code)

	/* Compare the real bundle with what is saved in the JIT area. */
	{
	 cmpeq  r0, r1, r0
	 mtspr  SPR_EX_CONTEXT_0_1, zero
	}

	/* Go to slow path if the fault bundle does not match. */
	beqz    r0, hand_unalign_slow

	/*
	 * A cache hit is found.
	 * r2 points to start of JIT code (3rd word).
	 * r0 is the fault pc.
	 * r1 is the fault bundle.
	 * Reset the low bit of sp.
	 */
	{
	 mfspr  r0, SPR_EX_CONTEXT_K_0
	 andi   sp, sp, ~1
	}

	/* Write r2 into EX_CONTEXT_K_0 and increment PC. */
	{
	 mtspr  SPR_EX_CONTEXT_K_0, r2
	 addi   r0, r0, 8
	}

	/*
	 * Set ICS on kernel EX_CONTEXT_K_1 in order to "iret" to
	 * user with ICS set. This way, if the JIT fixup causes another
	 * unalign exception (which shouldn't be possible) the user
	 * process will be terminated with SIGBUS. Also, our fixup will
	 * run without interleaving with external interrupts.
	 * Each fixup is at most 14 bundles, so it won't hold ICS for long.
	 */
	{
	 movei  r1, PL_ICS_EX1(USER_PL, 1)
	 mtspr  SPR_EX_CONTEXT_0_0, r0
	}

	{
	 mtspr  SPR_EX_CONTEXT_K_1, r1
	 addi   r3, r3, -(3 * 8)
	}

	/* Restore r0..r3. */
	ld_add  r0, r3, 8
	ld_add  r1, r3, 8
	ld_add  r2, r3, 8
	ld      r3, r3

	iret
	ENDPROC(intvec_\vecname)
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
	.macro __int_hand vecnum, vecname, c_routine,processing=handle_interrupt
intvec_\vecname:
	/* Temporarily save a register so we have somewhere to work. */

	mtspr   SPR_SYSTEM_SAVE_K_1, r0
	mfspr   r0, SPR_EX_CONTEXT_K_1

	/*
	 * The unalign data fastpath code sets the low bit in sp to
	 * force us to reset it here on fault.
	 */
	{
	 blbs   sp, 2f
	 IS_KERNEL_EX1(r0, r0)
	}

	.ifc    \vecnum, INT_DOUBLE_FAULT
	/*
	 * For double-faults from user-space, fall through to the normal
	 * register save and stack setup path.  Otherwise, it's the
	 * hypervisor giving us one last chance to dump diagnostics, and we
	 * branch to the kernel_double_fault routine to do so.
	 */
	beqz    r0, 1f
	j       _kernel_double_fault
1:
	.else
	/*
	 * If we're coming from user-space, then set sp to the top of
	 * the kernel stack.  Otherwise, assume sp is already valid.
	 */
	{
	 bnez   r0, 0f
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
	 *
	 * FIXME: if we end up never using this path, perhaps we should
	 * prevent the hypervisor from generating downcalls in this case.
	 * The advantage of getting a downcall is we can panic in Linux.
	 */
	mfspr   r0, SPR_SYSTEM_SAVE_K_2
	{
	 bltz   r0, 0f    /* high bit in S_S_1_2 is for a PC to use */
	 move   r0, sp
	}
	.endif

2:
	/*
	 * SYSTEM_SAVE_K_0 holds the cpu number in the high bits, and
	 * the current stack top in the lower bits.  So we recover
	 * our starting stack value by sign-extending the low bits, then
	 * point sp at the top aligned address on the actual stack page.
	 */
	mfspr   r0, SPR_SYSTEM_SAVE_K_0
	bfexts  r0, r0, 0, CPU_SHIFT-1

0:
	/*
	 * Align the stack mod 64 so we can properly predict what
	 * cache lines we need to write-hint to reduce memory fetch
	 * latency as we enter the kernel.  The layout of memory is
	 * as follows, with cache line 0 at the lowest VA, and cache
	 * line 8 just below the r0 value this "andi" computes.
	 * Note that we never write to cache line 8, and we skip
	 * cache lines 1-3 for syscalls.
	 *
	 *    cache line 8: ptregs padding (two words)
	 *    cache line 7: sp, lr, pc, ex1, faultnum, orig_r0, flags, cmpexch
	 *    cache line 6: r46...r53 (tp)
	 *    cache line 5: r38...r45
	 *    cache line 4: r30...r37
	 *    cache line 3: r22...r29
	 *    cache line 2: r14...r21
	 *    cache line 1: r6...r13
	 *    cache line 0: 2 x frame, r0..r5
	 */
#if STACK_TOP_DELTA != 64
#error STACK_TOP_DELTA must be 64 for assumptions here and in task_pt_regs()
#endif
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
	 * The backtracer needs pc, ex1, lr, sp, r52, and faultnum,
	 * and needs sp set to its final location at the bottom of
	 * the stack frame.
	 */
	addli   r0, r0, PTREGS_OFFSET_LR - (PTREGS_SIZE + KSTK_PTREGS_GAP)
	wh64    r0   /* cache line 7 */
	{
	 st     r0, lr
	 addli  r0, r0, PTREGS_OFFSET_SP - PTREGS_OFFSET_LR
	}
	{
	 st     r0, sp
	 addli  sp, r0, PTREGS_OFFSET_REG(52) - PTREGS_OFFSET_SP
	}
	wh64    sp   /* cache line 6 */
	{
	 st     sp, r52
	 addli  sp, sp, PTREGS_OFFSET_REG(1) - PTREGS_OFFSET_REG(52)
	}
	wh64    sp   /* cache line 0 */
	{
	 st     sp, r1
	 addli  sp, sp, PTREGS_OFFSET_REG(2) - PTREGS_OFFSET_REG(1)
	}
	{
	 st     sp, r2
	 addli  sp, sp, PTREGS_OFFSET_REG(3) - PTREGS_OFFSET_REG(2)
	}
	{
	 st     sp, r3
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
	 st     sp, r0
	 addli  sp, sp, PTREGS_OFFSET_EX1 - PTREGS_OFFSET_PC
	}
	mfspr   r0, SPR_EX_CONTEXT_K_1
	{
	 st     sp, r0
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
	 st     sp, r0
	 .else
	 movei  r1, \vecnum
	}
	{
	 st     sp, r1
	 .endif
	 addli  sp, sp, PTREGS_OFFSET_REG(0) - PTREGS_OFFSET_FAULTNUM
	}
	mfspr   r0, SPR_SYSTEM_SAVE_K_1    /* Original r0 */
	{
	 st     sp, r0
	 addi   sp, sp, -PTREGS_OFFSET_REG(0) - 8
	}
	{
	 st     sp, zero        /* write zero into "Next SP" frame pointer */
	 addi   sp, sp, -8      /* leave SP pointing at bottom of frame */
	}
	.ifc \processing,handle_syscall
	j       handle_syscall
	.else
	/* Capture per-interrupt SPR context to registers. */
	.ifc \c_routine, do_page_fault
	mfspr   r2, SPR_SYSTEM_SAVE_K_3   /* address of page fault */
	mfspr   r3, SPR_SYSTEM_SAVE_K_2   /* info about page fault */
	.else
	.ifc \vecnum, INT_ILL_TRANS
	mfspr   r2, ILL_VA_PC
	.else
	.ifc \vecnum, INT_DOUBLE_FAULT
	mfspr   r2, SPR_SYSTEM_SAVE_K_2   /* double fault info from HV */
	.else
	.ifc \c_routine, do_trap
	mfspr   r2, GPV_REASON
	.else
	.ifc \c_routine, handle_perf_interrupt
	mfspr   r2, PERF_COUNT_STS
	.else
	.ifc \c_routine, handle_perf_interrupt
	mfspr   r2, AUX_PERF_COUNT_STS
	.endif
	.ifc \c_routine, do_nmi
	mfspr   r2, SPR_SYSTEM_SAVE_K_2   /* nmi type */
	.else
	.endif
	.endif
	.endif
	.endif
	.endif
	.endif
	/* Put function pointer in r0 */
	moveli  r0, hw2_last(\c_routine)
	shl16insli r0, r0, hw1(\c_routine)
	{
	 shl16insli r0, r0, hw0(\c_routine)
	 j       \processing
	}
	.endif
	ENDPROC(intvec_\vecname)

#ifdef __COLLECT_LINKER_FEEDBACK__
	.pushsection .text.intvec_feedback,"ax"
	.org    (\vecnum << 5)
	FEEDBACK_ENTER_EXPLICIT(intvec_\vecname, .intrpt, 1 << 8)
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
	 st     r52, r0
	 .else
	 st     r52, zero
	 .endif
	 PTREGS_PTR(r52, PTREGS_OFFSET_TP)
	}
	st      r52, tp
	{
	 mfspr  tp, CMPEXCH_VALUE
	 PTREGS_PTR(r52, PTREGS_OFFSET_CMPEXCH)
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
	 st     r52, tp
	 PTREGS_PTR(r52, PTREGS_OFFSET_REG(33))
	}
	wh64    r52    /* cache line 4 */
	push_reg r33, r52
	push_reg r32, r52
	push_reg r31, r52
	.ifc \function,handle_syscall
	push_reg r30, r52, PTREGS_OFFSET_SYSCALL - PTREGS_OFFSET_REG(30)
	push_reg TREG_SYSCALL_NR_NAME, r52, \
	  PTREGS_OFFSET_REG(5) - PTREGS_OFFSET_SYSCALL
	.else

	push_reg r30, r52, PTREGS_OFFSET_REG(29) - PTREGS_OFFSET_REG(30)
	wh64    r52   /* cache line 3 */
	push_reg r29, r52
	push_reg r28, r52
	push_reg r27, r52
	push_reg r26, r52
	push_reg r25, r52
	push_reg r24, r52
	push_reg r23, r52
	push_reg r22, r52
	wh64    r52   /* cache line 2 */
	push_reg r21, r52
	push_reg r20, r52
	push_reg r19, r52
	push_reg r18, r52
	push_reg r17, r52
	push_reg r16, r52
	push_reg r15, r52
	push_reg r14, r52
	wh64    r52   /* cache line 1 */
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
	st      r52, r4

	/*
	 * If we will be returning to the kernel, we will need to
	 * reset the interrupt masks to the state they had before.
	 * Set DISABLE_IRQ in flags iff we came from kernel pl with
	 * irqs disabled.
	 */
	mfspr   r32, SPR_EX_CONTEXT_K_1
	{
	 IS_KERNEL_EX1(r32, r32)
	 PTREGS_PTR(r21, PTREGS_OFFSET_FLAGS)
	}
	beqzt   r32, 1f       /* zero if from user space */
	IRQS_DISABLED(r32)    /* zero if irqs enabled */
#if PT_FLAGS_DISABLE_IRQ != 1
# error Value of IRQS_DISABLED used to set PT_FLAGS_DISABLE_IRQ; fix
#endif
1:
	.ifnc \function,handle_syscall
	/* Record the fact that we saved the caller-save registers above. */
	ori     r32, r32, PT_FLAGS_CALLER_SAVES
	.endif
	st      r21, r32

	/*
	 * we've captured enough state to the stack (including in
	 * particular our EX_CONTEXT state) that we can now release
	 * the interrupt critical section and replace it with our
	 * standard "interrupts disabled" mask value.  This allows
	 * synchronous interrupts (and profile interrupts) to punch
	 * through from this point onwards.
	 *
	 * It's important that no code before this point touch memory
	 * other than our own stack (to keep the invariant that this
	 * is all that gets touched under ICS), and that no code after
	 * this point reference any interrupt-specific SPR, in particular
	 * the EX_CONTEXT_K_ values.
	 */
	.ifc \function,handle_nmi
	IRQ_DISABLE_ALL(r20)
	.else
	IRQ_DISABLE(r20, r21)
	.endif
	mtspr   INTERRUPT_CRITICAL_SECTION, zero

	/* Load tp with our per-cpu offset. */
#ifdef CONFIG_SMP
	{
	 mfspr  r20, SPR_SYSTEM_SAVE_K_0
	 moveli r21, hw2_last(__per_cpu_offset)
	}
	{
	 shl16insli r21, r21, hw1(__per_cpu_offset)
	 bfextu r20, r20, CPU_SHIFT, 63
	}
	shl16insli r21, r21, hw0(__per_cpu_offset)
	shl3add r20, r20, r21
	ld      tp, r20
#else
	move    tp, zero
#endif

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
	moveli  r21, hw2_last(intvec_feedback)
	shl16insli r21, r21, hw1(intvec_feedback)
	shl16insli r21, r21, hw0(intvec_feedback)
	add     r20, r20, r21
	jalr    r20

	/* And now notify the feedback routines that we are here. */
	FEEDBACK_ENTER(\function)
#endif

	/*
	 * Prepare the first 256 stack bytes to be rapidly accessible
	 * without having to fetch the background data.
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

#if defined(CONFIG_TRACE_IRQFLAGS) || defined(CONFIG_CONTEXT_TRACKING)
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
#ifdef CONFIG_CONTEXT_TRACKING
	jal     context_tracking_user_exit
#endif
	.ifnc \function,handle_syscall
	{ move r0, r30; move r1, r31 }
	{ move r2, r32; move r3, r33 }
	.endif
	.endif
#endif

	.endm

	/*
	 * Redispatch a downcall.
	 */
	.macro  dc_dispatch vecnum, vecname
	.org    (\vecnum << 8)
intvec_\vecname:
	j       _hv_downcall_dispatch
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
 *
 * Note that .Lresume_userspace is jumped to directly in several
 * places, and we need to make sure r30 is set correctly in those
 * callers as well.
 */
STD_ENTRY(interrupt_return)
	/* If we're resuming to kernel space, don't check thread flags. */
	{
	 bnez   r30, .Lrestore_all  /* NMIs don't special-case user-space */
	 PTREGS_PTR(r29, PTREGS_OFFSET_EX1)
	}
	ld      r29, r29
	IS_KERNEL_EX1(r29, r29)
	{
	 beqzt  r29, .Lresume_userspace
	 move   r29, sp
	}

#ifdef CONFIG_PREEMPT
	/* Returning to kernel space. Check if we need preemption. */
	EXTRACT_THREAD_INFO(r29)
	addli   r28, r29, THREAD_INFO_FLAGS_OFFSET
	{
	 ld     r28, r28
	 addli  r29, r29, THREAD_INFO_PREEMPT_COUNT_OFFSET
	}
	{
	 andi   r28, r28, _TIF_NEED_RESCHED
	 ld4s   r29, r29
	}
	beqzt   r28, 1f
	bnez    r29, 1f
	/* Disable interrupts explicitly for preemption. */
	IRQ_DISABLE(r20,r21)
	TRACE_IRQS_OFF
	jal     preempt_schedule_irq
	FEEDBACK_REENTER(interrupt_return)
1:
#endif

	/* If we're resuming to _cpu_idle_nap, bump PC forward by 8. */
	{
	 moveli r27, hw2_last(_cpu_idle_nap)
	 PTREGS_PTR(r29, PTREGS_OFFSET_PC)
	}
	{
	 ld     r28, r29
	 shl16insli r27, r27, hw1(_cpu_idle_nap)
	}
	{
	 shl16insli r27, r27, hw0(_cpu_idle_nap)
	}
	{
	 cmpeq  r27, r27, r28
	}
	{
	 blbc   r27, .Lrestore_all
	 addi   r28, r28, 8
	}
	st      r29, r28
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

	/*
	 * See if there are any work items (including single-shot items)
	 * to do.  If so, save the callee-save registers to pt_regs
	 * and then dispatch to C code.
	 */
	move    r21, sp
	EXTRACT_THREAD_INFO(r21)
	{
	 addi   r22, r21, THREAD_INFO_FLAGS_OFFSET
	 moveli r20, hw1_last(_TIF_ALLWORK_MASK)
	}
	{
	 ld     r22, r22
	 shl16insli r20, r20, hw0(_TIF_ALLWORK_MASK)
	}
	and     r1, r22, r20
	{
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	 beqzt  r1, .Lrestore_all
	}
	push_extra_callee_saves r0
	jal     prepare_exit_to_usermode

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
	 */
.Lrestore_all:
	PTREGS_PTR(r0, PTREGS_OFFSET_EX1)
	{
	 ld      r0, r0
	 PTREGS_PTR(r32, PTREGS_OFFSET_FLAGS)
	}
	{
	 IS_KERNEL_EX1(r0, r0)
	 ld     r32, r32
	}
	bnez    r0, 1f
	j       2f
#if PT_FLAGS_DISABLE_IRQ != 1
# error Assuming PT_FLAGS_DISABLE_IRQ == 1 so we can use blbct below
#endif
1:	blbct   r32, 2f
	IRQ_DISABLE(r20,r21)
	TRACE_IRQS_OFF
	movei   r0, 1
	mtspr   INTERRUPT_CRITICAL_SECTION, r0
	beqzt   r30, .Lrestore_regs
	j       3f
2:	TRACE_IRQS_ON
	IRQ_ENABLE_LOAD(r20, r21)
	movei   r0, 1
	mtspr   INTERRUPT_CRITICAL_SECTION, r0
	IRQ_ENABLE_APPLY(r20, r21)
	beqzt   r30, .Lrestore_regs
3:

#if INT_PERF_COUNT + 1 != INT_AUX_PERF_COUNT
# error Bad interrupt assumption
#endif
	{
	 movei  r0, 3   /* two adjacent bits for the PERF_COUNT mask */
	 beqz   r31, .Lrestore_regs
	}
	shli    r0, r0, INT_PERF_COUNT
	mtspr   SPR_INTERRUPT_MASK_RESET_K, r0

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
	 rotli  r20, r32, 62
	 PTREGS_PTR(sp, PTREGS_OFFSET_REG(0))
	}

	/*
	 * Load cache lines 0, 4, 6 and 7, in that order, then use
	 * the last loaded value, which makes it likely that the other
	 * cache lines have also loaded, at which point we should be
	 * able to safely read all the remaining words on those cache
	 * lines without waiting for the memory subsystem.
	 */
	pop_reg r0, sp, PTREGS_OFFSET_REG(30) - PTREGS_OFFSET_REG(0)
	pop_reg r30, sp, PTREGS_OFFSET_REG(52) - PTREGS_OFFSET_REG(30)
	pop_reg_zero r52, r3, sp, PTREGS_OFFSET_CMPEXCH - PTREGS_OFFSET_REG(52)
	pop_reg_zero r21, r27, sp, PTREGS_OFFSET_EX1 - PTREGS_OFFSET_CMPEXCH
	pop_reg_zero lr, r2, sp, PTREGS_OFFSET_PC - PTREGS_OFFSET_EX1
	{
	 mtspr  CMPEXCH_VALUE, r21
	 move   r4, zero
	}
	pop_reg r21, sp, PTREGS_OFFSET_REG(31) - PTREGS_OFFSET_PC
	{
	 mtspr  SPR_EX_CONTEXT_K_1, lr
	 IS_KERNEL_EX1(lr, lr)
	}
	{
	 mtspr  SPR_EX_CONTEXT_K_0, r21
	 move   r5, zero
	}

	/* Restore callee-saveds that we actually use. */
	pop_reg_zero r31, r6
	pop_reg_zero r32, r7
	pop_reg_zero r33, r8, sp, PTREGS_OFFSET_REG(29) - PTREGS_OFFSET_REG(33)

	/*
	 * If we modified other callee-saveds, restore them now.
	 * This is rare, but could be via ptrace or signal handler.
	 */
	{
	 move   r9, zero
	 blbs   r20, .Lrestore_callees
	}
.Lcontinue_restore_regs:

	/* Check if we're returning from a syscall. */
	{
	 move   r10, zero
	 bltzt  r20, 1f  /* no, so go restore callee-save registers */
	}

	/*
	 * Check if we're returning to userspace.
	 * Note that if we're not, we don't worry about zeroing everything.
	 */
	{
	 addli  sp, sp, PTREGS_OFFSET_LR - PTREGS_OFFSET_REG(29)
	 bnez   lr, .Lkernel_return
	}

	/*
	 * On return from syscall, we've restored r0 from pt_regs, but we
	 * clear the remainder of the caller-saved registers.  We could
	 * restore the syscall arguments, but there's not much point,
	 * and it ensures user programs aren't trying to use the
	 * caller-saves if we clear them, as well as avoiding leaking
	 * kernel pointers into userspace.
	 */
	pop_reg_zero lr, r11, sp, PTREGS_OFFSET_TP - PTREGS_OFFSET_LR
	pop_reg_zero tp, r12, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_TP
	{
	 ld     sp, sp
	 move   r13, zero
	 move   r14, zero
	}
	{ move r15, zero; move r16, zero }
	{ move r17, zero; move r18, zero }
	{ move r19, zero; move r20, zero }
	{ move r21, zero; move r22, zero }
	{ move r23, zero; move r24, zero }
	{ move r25, zero; move r26, zero }

	/* Set r1 to errno if we are returning an error, otherwise zero. */
	{
	 moveli r29, 4096
	 sub    r1, zero, r0
	}
	{
	 move   r28, zero
	 cmpltu r29, r1, r29
	}
	{
	 mnz    r1, r29, r1
	 move   r29, zero
	}
	iret

	/*
	 * Not a syscall, so restore caller-saved registers.
	 * First kick off loads for cache lines 1-3, which we're touching
	 * for the first time here.
	 */
	.align 64
1:	pop_reg r29, sp, PTREGS_OFFSET_REG(21) - PTREGS_OFFSET_REG(29)
	pop_reg r21, sp, PTREGS_OFFSET_REG(13) - PTREGS_OFFSET_REG(21)
	pop_reg r13, sp, PTREGS_OFFSET_REG(1) - PTREGS_OFFSET_REG(13)
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
	pop_reg r12, sp, 16
	/* r13 already restored above */
	pop_reg r14
	pop_reg r15
	pop_reg r16
	pop_reg r17
	pop_reg r18
	pop_reg r19
	pop_reg r20, sp, 16
	/* r21 already restored above */
	pop_reg r22
	pop_reg r23
	pop_reg r24
	pop_reg r25
	pop_reg r26
	pop_reg r27
	pop_reg r28, sp, PTREGS_OFFSET_LR - PTREGS_OFFSET_REG(28)
	/* r29 already restored above */
	bnez    lr, .Lkernel_return
	pop_reg lr, sp, PTREGS_OFFSET_TP - PTREGS_OFFSET_LR
	pop_reg tp, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_TP
	ld      sp, sp
	iret

	/*
	 * We can't restore tp when in kernel mode, since a thread might
	 * have migrated from another cpu and brought a stale tp value.
	 */
.Lkernel_return:
	pop_reg lr, sp, PTREGS_OFFSET_SP - PTREGS_OFFSET_LR
	ld      sp, sp
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
	STD_ENDPROC(interrupt_return)

	/*
	 * "NMI" interrupts mask ALL interrupts before calling the
	 * handler, and don't check thread flags, etc., on the way
	 * back out.  In general, the only things we do here for NMIs
	 * are register save/restore and dataplane kernel-TLB management.
	 * We don't (for example) deal with start/stop of the sched tick.
	 */
	.pushsection .text.handle_nmi,"ax"
handle_nmi:
	finish_interrupt_save handle_nmi
	{
	 jalr   r0
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	}
	FEEDBACK_REENTER(handle_nmi)
	{
	 movei  r30, 1
	 cmpeq  r31, r0, zero
	}
	j       interrupt_return
	STD_ENDPROC(handle_nmi)

	/*
	 * Parallel code for syscalls to handle_interrupt.
	 */
	.pushsection .text.handle_syscall,"ax"
handle_syscall:
	finish_interrupt_save handle_syscall

	/* Enable irqs. */
	TRACE_IRQS_ON
	IRQ_ENABLE(r20, r21)

	/* Bump the counter for syscalls made on this tile. */
	moveli r20, hw2_last(irq_stat + IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET)
	shl16insli r20, r20, hw1(irq_stat + IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET)
	shl16insli r20, r20, hw0(irq_stat + IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET)
	add     r20, r20, tp
	ld4s    r21, r20
	{
	 addi   r21, r21, 1
	 move   r31, sp
	}
	{
	 st4    r20, r21
	 EXTRACT_THREAD_INFO(r31)
	}

	/* Trace syscalls, if requested. */
	addi	r31, r31, THREAD_INFO_FLAGS_OFFSET
	{
	 ld     r30, r31
	 moveli r32, _TIF_SYSCALL_ENTRY_WORK
	}
	and     r30, r30, r32
	{
	 addi   r30, r31, THREAD_INFO_STATUS_OFFSET - THREAD_INFO_FLAGS_OFFSET
	 beqzt	r30, .Lrestore_syscall_regs
	}
	{
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	 jal    do_syscall_trace_enter
	}
	FEEDBACK_REENTER(handle_syscall)
	bltz    r0, .Lsyscall_sigreturn_skip

	/*
	 * We always reload our registers from the stack at this
	 * point.  They might be valid, if we didn't build with
	 * TRACE_IRQFLAGS, and this isn't a dataplane tile, and we're not
	 * doing syscall tracing, but there are enough cases now that it
	 * seems simplest just to do the reload unconditionally.
	 */
.Lrestore_syscall_regs:
	{
	 ld     r30, r30
	 PTREGS_PTR(r11, PTREGS_OFFSET_REG(0))
	}
	pop_reg r0,  r11
	pop_reg r1,  r11
	pop_reg r2,  r11
	pop_reg r3,  r11
	pop_reg r4,  r11
	pop_reg r5,  r11, PTREGS_OFFSET_SYSCALL - PTREGS_OFFSET_REG(5)
	{
	 ld     TREG_SYSCALL_NR_NAME, r11
	 moveli r21, __NR_syscalls
	}

	/* Ensure that the syscall number is within the legal range. */
	{
	 moveli r20, hw2(sys_call_table)
#ifdef CONFIG_COMPAT
	 blbs   r30, .Lcompat_syscall
#endif
	}
	{
	 cmpltu r21, TREG_SYSCALL_NR_NAME, r21
	 shl16insli r20, r20, hw1(sys_call_table)
	}
	{
	 blbc   r21, .Linvalid_syscall
	 shl16insli r20, r20, hw0(sys_call_table)
	}
.Lload_syscall_pointer:
	shl3add r20, TREG_SYSCALL_NR_NAME, r20
	ld      r20, r20

	/* Jump to syscall handler. */
	jalr    r20
.Lhandle_syscall_link: /* value of "lr" after "jalr r20" above */

	/*
	 * Write our r0 onto the stack so it gets restored instead
	 * of whatever the user had there before.
	 * In compat mode, sign-extend r0 before storing it.
	 */
	{
	 PTREGS_PTR(r29, PTREGS_OFFSET_REG(0))
	 blbct  r30, 1f
	}
	addxi   r0, r0, 0
1:	st      r29, r0

.Lsyscall_sigreturn_skip:
	FEEDBACK_REENTER(handle_syscall)

	/* Do syscall trace again, if requested. */
	{
	 ld      r30, r31
	 moveli  r32, _TIF_SYSCALL_EXIT_WORK
	}
	and      r0, r30, r32
	{
	 andi    r0, r30, _TIF_SINGLESTEP
	 beqzt   r0, 1f
	}
	{
	 PTREGS_PTR(r0, PTREGS_OFFSET_BASE)
	 jal    do_syscall_trace_exit
	}
	FEEDBACK_REENTER(handle_syscall)
	andi    r0, r30, _TIF_SINGLESTEP

1:	beqzt	r0, 2f

	/* Single stepping -- notify ptrace. */
	{
	 movei   r0, SIGTRAP
	 jal     ptrace_notify
	}
	FEEDBACK_REENTER(handle_syscall)

2:	{
	 movei  r30, 0               /* not an NMI */
	 j      .Lresume_userspace   /* jump into middle of interrupt_return */
	}

#ifdef CONFIG_COMPAT
.Lcompat_syscall:
	/*
	 * Load the base of the compat syscall table in r20, and
	 * range-check the syscall number (duplicated from 64-bit path).
	 * Sign-extend all the user's passed arguments to make them consistent.
	 * Also save the original "r(n)" values away in "r(11+n)" in
	 * case the syscall table entry wants to validate them.
	 */
	moveli  r20, hw2(compat_sys_call_table)
	{
	 cmpltu r21, TREG_SYSCALL_NR_NAME, r21
	 shl16insli r20, r20, hw1(compat_sys_call_table)
	}
	{
	 blbc   r21, .Linvalid_syscall
	 shl16insli r20, r20, hw0(compat_sys_call_table)
	}
	{ move r11, r0; addxi r0, r0, 0 }
	{ move r12, r1; addxi r1, r1, 0 }
	{ move r13, r2; addxi r2, r2, 0 }
	{ move r14, r3; addxi r3, r3, 0 }
	{ move r15, r4; addxi r4, r4, 0 }
	{ move r16, r5; addxi r5, r5, 0 }
	j .Lload_syscall_pointer
#endif

.Linvalid_syscall:
	/* Report an invalid syscall back to the user program */
	{
	 PTREGS_PTR(r29, PTREGS_OFFSET_REG(0))
	 movei  r28, -ENOSYS
	}
	st      r29, r28
	{
	 movei  r30, 0               /* not an NMI */
	 j      .Lresume_userspace   /* jump into middle of interrupt_return */
	}
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
	{
	 movei  r30, 0               /* not an NMI */
	 j      .Lresume_userspace   /* jump into middle of interrupt_return */
	}
	STD_ENDPROC(ret_from_fork)

STD_ENTRY(ret_from_kernel_thread)
	jal     sim_notify_fork
	jal     schedule_tail
	FEEDBACK_REENTER(ret_from_fork)
	{
	 move   r0, r31
	 jalr   r30
	}
	FEEDBACK_REENTER(ret_from_kernel_thread)
	{
	 movei  r30, 0               /* not an NMI */
	 j      interrupt_return
	}
	STD_ENDPROC(ret_from_kernel_thread)

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

PTREGS_SYSCALL_SIGRETURN(sys_rt_sigreturn, r0)
#ifdef CONFIG_COMPAT
PTREGS_SYSCALL_SIGRETURN(compat_sys_rt_sigreturn, r0)
#endif

/* Save additional callee-saves to pt_regs and jump to standard function. */
STD_ENTRY(_sys_clone)
	push_extra_callee_saves r4
	j       sys_clone
	STD_ENDPROC(_sys_clone)

	/*
	 * Recover r3, r2, r1 and r0 here saved by unalign fast vector.
	 * The vector area limit is 32 bundles, so we handle the reload here.
	 * r0, r1, r2 are in thread_info from low to high memory in order.
	 * r3 points to location the original r3 was saved.
	 * We put this code in the __HEAD section so it can be reached
	 * via a conditional branch from the fast path.
	 */
	__HEAD
hand_unalign_slow:
	andi    sp, sp, ~1
hand_unalign_slow_badsp:
	addi    r3, r3, -(3 * 8)
	ld_add  r0, r3, 8
	ld_add  r1, r3, 8
	ld      r2, r3
hand_unalign_slow_nonuser:
	mfspr   r3, SPR_SYSTEM_SAVE_K_1
	__int_hand     INT_UNALIGN_DATA, UNALIGN_DATA_SLOW, int_unalign

/* The unaligned data support needs to read all the registers. */
int_unalign:
	push_extra_callee_saves r0
	j       do_unaligned
ENDPROC(hand_unalign_slow)

/* Fill the return address stack with nonzero entries. */
STD_ENTRY(fill_ra_stack)
	{
	 move	r0, lr
	 jal	1f
	}
1:	jal	2f
2:	jal	3f
3:	jal	4f
4:	jrp	r0
	STD_ENDPROC(fill_ra_stack)

	.macro int_hand  vecnum, vecname, c_routine, processing=handle_interrupt
	.org   (\vecnum << 8)
		__int_hand   \vecnum, \vecname, \c_routine, \processing
	.endm

/* Include .intrpt array of interrupt vectors */
	.section ".intrpt", "ax"
	.global intrpt_start
intrpt_start:

#ifndef CONFIG_USE_PMC
#define handle_perf_interrupt bad_intr
#endif

#ifndef CONFIG_HARDWALL
#define do_hardwall_trap bad_intr
#endif

	int_hand     INT_MEM_ERROR, MEM_ERROR, do_trap
	int_hand     INT_SINGLE_STEP_3, SINGLE_STEP_3, bad_intr
#if CONFIG_KERNEL_PL == 2
	int_hand     INT_SINGLE_STEP_2, SINGLE_STEP_2, gx_singlestep_handle
	int_hand     INT_SINGLE_STEP_1, SINGLE_STEP_1, bad_intr
#else
	int_hand     INT_SINGLE_STEP_2, SINGLE_STEP_2, bad_intr
	int_hand     INT_SINGLE_STEP_1, SINGLE_STEP_1, gx_singlestep_handle
#endif
	int_hand     INT_SINGLE_STEP_0, SINGLE_STEP_0, bad_intr
	int_hand     INT_IDN_COMPLETE, IDN_COMPLETE, bad_intr
	int_hand     INT_UDN_COMPLETE, UDN_COMPLETE, bad_intr
	int_hand     INT_ITLB_MISS, ITLB_MISS, do_page_fault
	int_hand     INT_ILL, ILL, do_trap
	int_hand     INT_GPV, GPV, do_trap
	int_hand     INT_IDN_ACCESS, IDN_ACCESS, do_trap
	int_hand     INT_UDN_ACCESS, UDN_ACCESS, do_trap
	int_hand     INT_SWINT_3, SWINT_3, do_trap
	int_hand     INT_SWINT_2, SWINT_2, do_trap
	int_hand     INT_SWINT_1, SWINT_1, SYSCALL, handle_syscall
	int_hand     INT_SWINT_0, SWINT_0, do_trap
	int_hand     INT_ILL_TRANS, ILL_TRANS, do_trap
	int_hand_unalign_fast INT_UNALIGN_DATA, UNALIGN_DATA
	int_hand     INT_DTLB_MISS, DTLB_MISS, do_page_fault
	int_hand     INT_DTLB_ACCESS, DTLB_ACCESS, do_page_fault
	int_hand     INT_IDN_FIREWALL, IDN_FIREWALL, do_hardwall_trap
	int_hand     INT_UDN_FIREWALL, UDN_FIREWALL, do_hardwall_trap
	int_hand     INT_TILE_TIMER, TILE_TIMER, do_timer_interrupt
	int_hand     INT_IDN_TIMER, IDN_TIMER, bad_intr
	int_hand     INT_UDN_TIMER, UDN_TIMER, bad_intr
	int_hand     INT_IDN_AVAIL, IDN_AVAIL, bad_intr
	int_hand     INT_UDN_AVAIL, UDN_AVAIL, bad_intr
	int_hand     INT_IPI_3, IPI_3, bad_intr
#if CONFIG_KERNEL_PL == 2
	int_hand     INT_IPI_2, IPI_2, tile_dev_intr
	int_hand     INT_IPI_1, IPI_1, bad_intr
#else
	int_hand     INT_IPI_2, IPI_2, bad_intr
	int_hand     INT_IPI_1, IPI_1, tile_dev_intr
#endif
	int_hand     INT_IPI_0, IPI_0, bad_intr
	int_hand     INT_PERF_COUNT, PERF_COUNT, \
		     handle_perf_interrupt, handle_nmi
	int_hand     INT_AUX_PERF_COUNT, AUX_PERF_COUNT, \
		     handle_perf_interrupt, handle_nmi
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
	int_hand     INT_DEV_INTR_DWNCL, DEV_INTR_DWNCL, bad_intr
	int_hand     INT_I_ASID, I_ASID, bad_intr
	int_hand     INT_D_ASID, D_ASID, bad_intr
	int_hand     INT_DOUBLE_FAULT, DOUBLE_FAULT, do_trap

	/* Synthetic interrupt delivered only by the simulator */
	int_hand     INT_BREAKPOINT, BREAKPOINT, do_breakpoint
	/* Synthetic interrupt delivered by hv */
	int_hand     INT_NMI_DWNCL, NMI_DWNCL, do_nmi, handle_nmi
