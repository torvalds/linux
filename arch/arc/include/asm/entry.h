/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: Aug 28th 2008: Bug #94984
 *  -Zero Overhead Loop Context shd be cleared when entering IRQ/EXcp/Trap
 *   Normally CPU does this automatically, however when doing FAKE rtie,
 *   we also need to explicitly do this. The problem in macros
 *   FAKE_RET_FROM_EXCPN and FAKE_RET_FROM_EXCPN_LOCK_IRQ was that this bit
 *   was being "CLEARED" rather then "SET". Actually "SET" clears ZOL context
 *
 * Vineetg: May 5th 2008
 *  - Defined Stack Switching Macro to be reused in all intr/excp hdlrs
 *  - Shaved off 11 instructions from RESTORE_ALL_INT1 by using the
 *      address Write back load ld.ab instead of seperate ld/add instn
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef __ASM_ARC_ENTRY_H
#define __ASM_ARC_ENTRY_H

#ifdef __ASSEMBLY__
#include <asm/unistd.h>		/* For NR_syscalls defination */
#include <asm/asm-offsets.h>
#include <asm/arcregs.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>	/* For THREAD_SIZE */

/* Note on the LD/ST addr modes with addr reg wback
 *
 * LD.a same as LD.aw
 *
 * LD.a    reg1, [reg2, x]  => Pre Incr
 *      Eff Addr for load = [reg2 + x]
 *
 * LD.ab   reg1, [reg2, x]  => Post Incr
 *      Eff Addr for load = [reg2]
 */

/*--------------------------------------------------------------
 * Save caller saved registers (scratch registers) ( r0 - r12 )
 * Registers are pushed / popped in the order defined in struct ptregs
 * in asm/ptrace.h
 *-------------------------------------------------------------*/
.macro  SAVE_CALLER_SAVED
	st.a    r0, [sp, -4]
	st.a    r1, [sp, -4]
	st.a    r2, [sp, -4]
	st.a    r3, [sp, -4]
	st.a    r4, [sp, -4]
	st.a    r5, [sp, -4]
	st.a    r6, [sp, -4]
	st.a    r7, [sp, -4]
	st.a    r8, [sp, -4]
	st.a    r9, [sp, -4]
	st.a    r10, [sp, -4]
	st.a    r11, [sp, -4]
	st.a    r12, [sp, -4]
.endm

/*--------------------------------------------------------------
 * Restore caller saved registers (scratch registers)
 *-------------------------------------------------------------*/
.macro RESTORE_CALLER_SAVED
	ld.ab   r12, [sp, 4]
	ld.ab   r11, [sp, 4]
	ld.ab   r10, [sp, 4]
	ld.ab   r9, [sp, 4]
	ld.ab   r8, [sp, 4]
	ld.ab   r7, [sp, 4]
	ld.ab   r6, [sp, 4]
	ld.ab   r5, [sp, 4]
	ld.ab   r4, [sp, 4]
	ld.ab   r3, [sp, 4]
	ld.ab   r2, [sp, 4]
	ld.ab   r1, [sp, 4]
	ld.ab   r0, [sp, 4]
.endm


/*--------------------------------------------------------------
 * Save callee saved registers (non scratch registers) ( r13 - r25 )
 *  on kernel stack.
 * User mode callee regs need to be saved in case of
 *    -fork and friends for replicating from parent to child
 *    -before going into do_signal( ) for ptrace/core-dump
 * Special case handling is required for r25 in case it is used by kernel
 *  for caching task ptr. Low level exception/ISR save user mode r25
 *  into task->thread.user_r25. So it needs to be retrieved from there and
 *  saved into kernel stack with rest of callee reg-file
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_USER
	st.a    r13, [sp, -4]
	st.a    r14, [sp, -4]
	st.a    r15, [sp, -4]
	st.a    r16, [sp, -4]
	st.a    r17, [sp, -4]
	st.a    r18, [sp, -4]
	st.a    r19, [sp, -4]
	st.a    r20, [sp, -4]
	st.a    r21, [sp, -4]
	st.a    r22, [sp, -4]
	st.a    r23, [sp, -4]
	st.a    r24, [sp, -4]
	st.a    r25, [sp, -4]

	/* move up by 1 word to "create" callee_regs->"stack_place_holder" */
	sub sp, sp, 4
.endm

/*--------------------------------------------------------------
 * Save callee saved registers (non scratch registers) ( r13 - r25 )
 * kernel mode callee regs needed to be saved in case of context switch
 * If r25 is used for caching task pointer then that need not be saved
 * as it can be re-created from current task global
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_KERNEL
	st.a    r13, [sp, -4]
	st.a    r14, [sp, -4]
	st.a    r15, [sp, -4]
	st.a    r16, [sp, -4]
	st.a    r17, [sp, -4]
	st.a    r18, [sp, -4]
	st.a    r19, [sp, -4]
	st.a    r20, [sp, -4]
	st.a    r21, [sp, -4]
	st.a    r22, [sp, -4]
	st.a    r23, [sp, -4]
	st.a    r24, [sp, -4]
	st.a    r25, [sp, -4]
	sub     sp, sp, 4
.endm

/*--------------------------------------------------------------
 * RESTORE_CALLEE_SAVED_KERNEL:
 * Loads callee (non scratch) Reg File by popping from Kernel mode stack.
 *  This is reverse of SAVE_CALLEE_SAVED,
 *
 * NOTE:
 * Ideally this shd only be called in switch_to for loading
 *  switched-IN task's CALLEE Reg File.
 *  For all other cases RESTORE_CALLEE_SAVED_FAST must be used
 *  which simply pops the stack w/o touching regs.
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_KERNEL

	add     sp, sp, 4   /* skip "callee_regs->stack_place_holder" */
	ld.ab   r25, [sp, 4]
	ld.ab   r24, [sp, 4]
	ld.ab   r23, [sp, 4]
	ld.ab   r22, [sp, 4]
	ld.ab   r21, [sp, 4]
	ld.ab   r20, [sp, 4]
	ld.ab   r19, [sp, 4]
	ld.ab   r18, [sp, 4]
	ld.ab   r17, [sp, 4]
	ld.ab   r16, [sp, 4]
	ld.ab   r15, [sp, 4]
	ld.ab   r14, [sp, 4]
	ld.ab   r13, [sp, 4]

.endm

/*--------------------------------------------------------------
 * RESTORE_CALLEE_SAVED_USER:
 * This is called after do_signal where tracer might have changed callee regs
 * thus we need to restore the reg file.
 * Special case handling is required for r25 in case it is used by kernel
 *  for caching task ptr. Ptrace would have modified on-kernel-stack value of
 *  r25, which needs to be shoved back into task->thread.user_r25 where from
 *  Low level exception/ISR return code will retrieve to populate with rest of
 *  callee reg-file.
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_USER

	add     sp, sp, 4   /* skip "callee_regs->stack_place_holder" */

#ifdef CONFIG_ARC_CURR_IN_REG
	ld.ab   r12, [sp, 4]
	st      r12, [r25, TASK_THREAD + THREAD_USER_R25]
#else
	ld.ab   r25, [sp, 4]
#endif

	ld.ab   r24, [sp, 4]
	ld.ab   r23, [sp, 4]
	ld.ab   r22, [sp, 4]
	ld.ab   r21, [sp, 4]
	ld.ab   r20, [sp, 4]
	ld.ab   r19, [sp, 4]
	ld.ab   r18, [sp, 4]
	ld.ab   r17, [sp, 4]
	ld.ab   r16, [sp, 4]
	ld.ab   r15, [sp, 4]
	ld.ab   r14, [sp, 4]
	ld.ab   r13, [sp, 4]
.endm

/*--------------------------------------------------------------
 * Super FAST Restore callee saved regs by simply re-adjusting SP
 *-------------------------------------------------------------*/
.macro DISCARD_CALLEE_SAVED_USER
	add     sp, sp, 14 * 4
.endm

/*--------------------------------------------------------------
 * Restore User mode r25 saved in task_struct->thread.user_r25
 *-------------------------------------------------------------*/
.macro RESTORE_USER_R25
	ld  r25, [r25, TASK_THREAD + THREAD_USER_R25]
.endm

/*-------------------------------------------------------------
 * given a tsk struct, get to the base of it's kernel mode stack
 * tsk->thread_info is really a PAGE, whose bottom hoists stack
 * which grows upwards towards thread_info
 *------------------------------------------------------------*/

.macro GET_TSK_STACK_BASE tsk, out

	/* Get task->thread_info (this is essentially start of a PAGE) */
	ld  \out, [\tsk, TASK_THREAD_INFO]

	/* Go to end of page where stack begins (grows upwards) */
	add2 \out, \out, (THREAD_SIZE - 4)/4   /* one word GUTTER */

.endm

/*--------------------------------------------------------------
 * Switch to Kernel Mode stack if SP points to User Mode stack
 *
 * Entry   : r9 contains pre-IRQ/exception/trap status32
 * Exit    : SP is set to kernel mode stack pointer
 * Clobbers: r9
 *-------------------------------------------------------------*/

.macro SWITCH_TO_KERNEL_STK

	/* User Mode when this happened ? Yes: Proceed to switch stack */
	bbit1   r9, STATUS_U_BIT, 88f

	/* OK we were already in kernel mode when this event happened, thus can
	 * assume SP is kernel mode SP. _NO_ need to do any stack switching
	 */

	/* Save Pre Intr/Exception KERNEL MODE SP on kernel stack
	 * safe-keeping not really needed, but it keeps the epilogue code
	 * (SP restore) simpler/uniform.
	 */
	b.d	77f

	st.a	sp, [sp, -12]	; Make room for orig_r0 and orig_r8

88: /*------Intr/Ecxp happened in user mode, "switch" stack ------ */

	GET_CURR_TASK_ON_CPU   r9

	/* With current tsk in r9, get it's kernel mode stack base */
	GET_TSK_STACK_BASE  r9, r9

#ifdef PT_REGS_CANARY
	st	0xabcdabcd, [r9, 0]
#endif

	/* Save Pre Intr/Exception User SP on kernel stack */
	st.a    sp, [r9, -12]	; Make room for orig_r0 and orig_r8

	/* CAUTION:
	 * SP should be set at the very end when we are done with everything
	 * In case of 2 levels of interrupt we depend on value of SP to assume
	 * that everything else is done (loading r25 etc)
	 */

	/* set SP to point to kernel mode stack */
	mov sp, r9

77: /* ----- Stack Switched to kernel Mode, Now save REG FILE ----- */

.endm

/*------------------------------------------------------------
 * "FAKE" a rtie to return from CPU Exception context
 * This is to re-enable Exceptions within exception
 * Look at EV_ProtV to see how this is actually used
 *-------------------------------------------------------------*/

.macro FAKE_RET_FROM_EXCPN  reg

	ld  \reg, [sp, PT_status32]
	bic  \reg, \reg, (STATUS_U_MASK|STATUS_DE_MASK)
	bset \reg, \reg, STATUS_L_BIT
	sr  \reg, [erstatus]
	mov \reg, 55f
	sr  \reg, [eret]

	rtie
55:
.endm

/*
 * @reg [OUT] &thread_info of "current"
 */
.macro GET_CURR_THR_INFO_FROM_SP  reg
	and \reg, sp, ~(THREAD_SIZE - 1)
.endm

/*
 * @reg [OUT] thread_info->flags of "current"
 */
.macro GET_CURR_THR_INFO_FLAGS  reg
	GET_CURR_THR_INFO_FROM_SP  \reg
	ld  \reg, [\reg, THREAD_INFO_FLAGS]
.endm

/*--------------------------------------------------------------
 * For early Exception Prologue, a core reg is temporarily needed to
 * code the rest of prolog (stack switching). This is done by stashing
 * it to memory (non-SMP case) or SCRATCH0 Aux Reg (SMP).
 *
 * Before saving the full regfile - this reg is restored back, only
 * to be saved again on kernel mode stack, as part of ptregs.
 *-------------------------------------------------------------*/
.macro EXCPN_PROLOG_FREEUP_REG	reg
	st  \reg, [@ex_saved_reg1]
.endm

.macro EXCPN_PROLOG_RESTORE_REG	reg
	ld  \reg, [@ex_saved_reg1]
.endm

/*--------------------------------------------------------------
 * Save all registers used by Exceptions (TLB Miss, Prot-V, Mem err etc)
 * Requires SP to be already switched to kernel mode Stack
 * sp points to the next free element on the stack at exit of this macro.
 * Registers are pushed / popped in the order defined in struct ptregs
 * in asm/ptrace.h
 * Note that syscalls are implemented via TRAP which is also a exception
 * from CPU's point of view
 *-------------------------------------------------------------*/
.macro SAVE_ALL_EXCEPTION   marker

	st      \marker, [sp, 8]
	st      r0, [sp, 4]    /* orig_r0, needed only for sys calls */

	/* Restore r9 used to code the early prologue */
	EXCPN_PROLOG_RESTORE_REG  r9

	SAVE_CALLER_SAVED
	st.a    r26, [sp, -4]   /* gp */
	st.a    fp, [sp, -4]
	st.a    blink, [sp, -4]
	lr	r9, [eret]
	st.a    r9, [sp, -4]
	lr	r9, [erstatus]
	st.a    r9, [sp, -4]
	st.a    lp_count, [sp, -4]
	lr	r9, [lp_end]
	st.a    r9, [sp, -4]
	lr	r9, [lp_start]
	st.a    r9, [sp, -4]
	lr	r9, [erbta]
	st.a    r9, [sp, -4]

#ifdef PT_REGS_CANARY
	mov   r9, 0xdeadbeef
	st    r9, [sp, -4]
#endif

	/* move up by 1 word to "create" pt_regs->"stack_place_holder" */
	sub sp, sp, 4
.endm

/*--------------------------------------------------------------
 * Save scratch regs for exceptions
 *-------------------------------------------------------------*/
.macro SAVE_ALL_SYS
	SAVE_ALL_EXCEPTION  orig_r8_IS_EXCPN
.endm

/*--------------------------------------------------------------
 * Save scratch regs for sys calls
 *-------------------------------------------------------------*/
.macro SAVE_ALL_TRAP
	/*
	 * Setup pt_regs->orig_r8.
	 * Encode syscall number (r8) in upper short word of event type (r9)
	 * N.B. #1: This is already endian safe (see ptrace.h)
	 *      #2: Only r9 can be used as scratch as it is already clobbered
	 *          and it's contents are no longer needed by the latter part
	 *          of exception prologue
	 */
	lsl  r9, r8, 16
	or   r9, r9, orig_r8_IS_SCALL

	SAVE_ALL_EXCEPTION  r9
.endm

/*--------------------------------------------------------------
 * Restore all registers used by system call or Exceptions
 * SP should always be pointing to the next free stack element
 * when entering this macro.
 *
 * NOTE:
 *
 * It is recommended that lp_count/ilink1/ilink2 not be used as a dest reg
 * for memory load operations. If used in that way interrupts are deffered
 * by hardware and that is not good.
 *-------------------------------------------------------------*/
.macro RESTORE_ALL_SYS

	add sp, sp, 4       /* hop over unused "pt_regs->stack_place_holder" */

	ld.ab   r9, [sp, 4]
	sr	r9, [erbta]
	ld.ab   r9, [sp, 4]
	sr	r9, [lp_start]
	ld.ab   r9, [sp, 4]
	sr	r9, [lp_end]
	ld.ab   r9, [sp, 4]
	mov	lp_count, r9
	ld.ab   r9, [sp, 4]
	sr	r9, [erstatus]
	ld.ab   r9, [sp, 4]
	sr	r9, [eret]
	ld.ab   blink, [sp, 4]
	ld.ab   fp, [sp, 4]
	ld.ab   r26, [sp, 4]    /* gp */
	RESTORE_CALLER_SAVED

	ld  sp, [sp] /* restore original sp */
	/* orig_r0 and orig_r8 skipped automatically */
.endm


/*--------------------------------------------------------------
 * Save all registers used by interrupt handlers.
 *-------------------------------------------------------------*/
.macro SAVE_ALL_INT1

	/* restore original r9 , saved in int1_saved_reg
	* It will be saved on stack in macro: SAVE_CALLER_SAVED
	*/
	ld  r9, [@int1_saved_reg]

	/* now we are ready to save the remaining context :) */
	st      orig_r8_IS_IRQ1, [sp, 8]    /* Event Type */
	st      0, [sp, 4]    /* orig_r0 , N/A for IRQ */
	SAVE_CALLER_SAVED
	st.a    r26, [sp, -4]   /* gp */
	st.a    fp, [sp, -4]
	st.a    blink, [sp, -4]
	st.a    ilink1, [sp, -4]
	lr	r9, [status32_l1]
	st.a    r9, [sp, -4]
	st.a    lp_count, [sp, -4]
	lr	r9, [lp_end]
	st.a    r9, [sp, -4]
	lr	r9, [lp_start]
	st.a    r9, [sp, -4]
	lr	r9, [bta_l1]
	st.a    r9, [sp, -4]

#ifdef PT_REGS_CANARY
	mov   r9, 0xdeadbee1
	st    r9, [sp, -4]
#endif
	/* move up by 1 word to "create" pt_regs->"stack_place_holder" */
	sub sp, sp, 4
.endm

/*--------------------------------------------------------------
 * Restore all registers used by interrupt handlers.
 *
 * NOTE:
 *
 * It is recommended that lp_count/ilink1/ilink2 not be used as a dest reg
 * for memory load operations. If used in that way interrupts are deffered
 * by hardware and that is not good.
 *-------------------------------------------------------------*/

.macro RESTORE_ALL_INT1
	add sp, sp, 4       /* hop over unused "pt_regs->stack_place_holder" */

	ld.ab   r9, [sp, 4] /* Actual reg file */
	sr	r9, [bta_l1]
	ld.ab   r9, [sp, 4]
	sr	r9, [lp_start]
	ld.ab   r9, [sp, 4]
	sr	r9, [lp_end]
	ld.ab   r9, [sp, 4]
	mov	lp_count, r9
	ld.ab   r9, [sp, 4]
	sr	r9, [status32_l1]
	ld.ab   r9, [sp, 4]
	mov	ilink1, r9
	ld.ab   blink, [sp, 4]
	ld.ab   fp, [sp, 4]
	ld.ab   r26, [sp, 4]    /* gp */
	RESTORE_CALLER_SAVED

	ld  sp, [sp] /* restore original sp */
	/* orig_r0 and orig_r8 skipped automatically */
.endm

/* Get CPU-ID of this core */
.macro  GET_CPU_ID  reg
	lr  \reg, [identity]
	lsr \reg, \reg, 8
	bmsk \reg, \reg, 7
.endm

.macro  GET_CURR_TASK_ON_CPU    reg
	ld  \reg, [@_current_task]
.endm

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	st  \tsk, [@_current_task]
.endm

/* ------------------------------------------------------------------
 * Get the ptr to some field of Current Task at @off in task struct
 */

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	GET_CURR_TASK_ON_CPU  \reg
	add \reg, \reg, \off
.endm

#endif  /* __ASSEMBLY__ */

#endif  /* __ASM_ARC_ENTRY_H */
