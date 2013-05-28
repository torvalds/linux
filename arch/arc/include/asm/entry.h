/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Vineetg: March 2009 (Supporting 2 levels of Interrupts)
 *  Stack switching code can no longer reliably rely on the fact that
 *  if we are NOT in user mode, stack is switched to kernel mode.
 *  e.g. L2 IRQ interrupted a L1 ISR which had not yet completed
 *  it's prologue including stack switching from user mode
 *
 * Vineetg: Aug 28th 2008: Bug #94984
 *  -Zero Overhead Loop Context shd be cleared when entering IRQ/EXcp/Trap
 *   Normally CPU does this automatically, however when doing FAKE rtie,
 *   we also need to explicitly do this. The problem in macros
 *   FAKE_RET_FROM_EXCPN and FAKE_RET_FROM_EXCPN_LOCK_IRQ was that this bit
 *   was being "CLEARED" rather then "SET". Actually "SET" clears ZOL context
 *
 * Vineetg: May 5th 2008
 *  -Modified CALLEE_REG save/restore macros to handle the fact that
 *      r25 contains the kernel current task ptr
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
#include <asm/processor.h>	/* For VMALLOC_START */
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

.macro PUSH reg
	st.a	\reg, [sp, -4]
.endm

.macro PUSHAX aux
	lr	r9, [\aux]
	PUSH	r9
.endm

.macro POP reg
	ld.ab	\reg, [sp, 4]
.endm

.macro POPAX aux
	POP	r9
	sr	r9, [\aux]
.endm

/*--------------------------------------------------------------
 * Helpers to save/restore Scratch Regs:
 * used by Interrupt/Exception Prologue/Epilogue
 *-------------------------------------------------------------*/
.macro  SAVE_R0_TO_R12
	PUSH	r0
	PUSH	r1
	PUSH	r2
	PUSH	r3
	PUSH	r4
	PUSH	r5
	PUSH	r6
	PUSH	r7
	PUSH	r8
	PUSH	r9
	PUSH	r10
	PUSH	r11
	PUSH	r12
.endm

.macro RESTORE_R12_TO_R0
	POP	r12
	POP	r11
	POP	r10
	POP	r9
	POP	r8
	POP	r7
	POP	r6
	POP	r5
	POP	r4
	POP	r3
	POP	r2
	POP	r1
	POP	r0

#ifdef CONFIG_ARC_CURR_IN_REG
	ld	r25, [sp, 12]
#endif
.endm

/*--------------------------------------------------------------
 * Helpers to save/restore callee-saved regs:
 * used by several macros below
 *-------------------------------------------------------------*/
.macro SAVE_R13_TO_R24
	PUSH	r13
	PUSH	r14
	PUSH	r15
	PUSH	r16
	PUSH	r17
	PUSH	r18
	PUSH	r19
	PUSH	r20
	PUSH	r21
	PUSH	r22
	PUSH	r23
	PUSH	r24
.endm

.macro RESTORE_R24_TO_R13
	POP	r24
	POP	r23
	POP	r22
	POP	r21
	POP	r20
	POP	r19
	POP	r18
	POP	r17
	POP	r16
	POP	r15
	POP	r14
	POP	r13
.endm

#define OFF_USER_R25_FROM_R24	(SZ_CALLEE_REGS + SZ_PT_REGS - 8)/4

/*--------------------------------------------------------------
 * Collect User Mode callee regs as struct callee_regs - needed by
 * fork/do_signal/unaligned-access-emulation.
 * (By default only scratch regs are saved on entry to kernel)
 *
 * Special handling for r25 if used for caching Task Pointer.
 * It would have been saved in task->thread.user_r25 already, but to keep
 * the interface same it is copied into regular r25 placeholder in
 * struct callee_regs.
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_USER

	SAVE_R13_TO_R24

#ifdef CONFIG_ARC_CURR_IN_REG
	; Retrieve orig r25 and save it on stack
	ld.as   r12, [sp, OFF_USER_R25_FROM_R24]
	st.a    r12, [sp, -4]
#else
	PUSH	r25
#endif

.endm

/*--------------------------------------------------------------
 * Save kernel Mode callee regs at the time of Contect Switch.
 *
 * Special handling for r25 if used for caching Task Pointer.
 * Kernel simply skips saving it since it will be loaded with
 * incoming task pointer anyways
 *-------------------------------------------------------------*/
.macro SAVE_CALLEE_SAVED_KERNEL

	SAVE_R13_TO_R24

#ifdef CONFIG_ARC_CURR_IN_REG
	sub     sp, sp, 4
#else
	PUSH	r25
#endif
.endm

/*--------------------------------------------------------------
 * Opposite of SAVE_CALLEE_SAVED_KERNEL
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_KERNEL

#ifdef CONFIG_ARC_CURR_IN_REG
	add     sp, sp, 4  /* skip usual r25 placeholder */
#else
	POP	r25
#endif
	RESTORE_R24_TO_R13
.endm

/*--------------------------------------------------------------
 * Opposite of SAVE_CALLEE_SAVED_USER
 *
 * ptrace tracer or unaligned-access fixup might have changed a user mode
 * callee reg which is saved back to usual r25 storage location
 *-------------------------------------------------------------*/
.macro RESTORE_CALLEE_SAVED_USER

#ifdef CONFIG_ARC_CURR_IN_REG
	ld.ab   r12, [sp, 4]
	st.as   r12, [sp, OFF_USER_R25_FROM_R24]
#else
	POP	r25
#endif
	RESTORE_R24_TO_R13
.endm

/*--------------------------------------------------------------
 * Super FAST Restore callee saved regs by simply re-adjusting SP
 *-------------------------------------------------------------*/
.macro DISCARD_CALLEE_SAVED_USER
	add     sp, sp, SZ_CALLEE_REGS
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
	add2 \out, \out, (THREAD_SIZE)/4

.endm

/*--------------------------------------------------------------
 * Switch to Kernel Mode stack if SP points to User Mode stack
 *
 * Entry   : r9 contains pre-IRQ/exception/trap status32
 * Exit    : SP is set to kernel mode stack pointer
 *           If CURR_IN_REG, r25 set to "current" task pointer
 * Clobbers: r9
 *-------------------------------------------------------------*/

.macro SWITCH_TO_KERNEL_STK

	/* User Mode when this happened ? Yes: Proceed to switch stack */
	bbit1   r9, STATUS_U_BIT, 88f

	/* OK we were already in kernel mode when this event happened, thus can
	 * assume SP is kernel mode SP. _NO_ need to do any stack switching
	 */

#ifdef CONFIG_ARC_COMPACT_IRQ_LEVELS
	/* However....
	 * If Level 2 Interrupts enabled, we may end up with a corner case:
	 * 1. User Task executing
	 * 2. L1 IRQ taken, ISR starts (CPU auto-switched to KERNEL mode)
	 * 3. But before it could switch SP from USER to KERNEL stack
	 *      a L2 IRQ "Interrupts" L1
	 * Thay way although L2 IRQ happened in Kernel mode, stack is still
	 * not switched.
	 * To handle this, we may need to switch stack even if in kernel mode
	 * provided SP has values in range of USER mode stack ( < 0x7000_0000 )
	 */
	brlo sp, VMALLOC_START, 88f

	/* TODO: vineetg:
	 * We need to be a bit more cautious here. What if a kernel bug in
	 * L1 ISR, caused SP to go whaco (some small value which looks like
	 * USER stk) and then we take L2 ISR.
	 * Above brlo alone would treat it as a valid L1-L2 sceanrio
	 * instead of shouting alound
	 * The only feasible way is to make sure this L2 happened in
	 * L1 prelogue ONLY i.e. ilink2 is less than a pre-set marker in
	 * L1 ISR before it switches stack
	 */

#endif

	/* Save Pre Intr/Exception KERNEL MODE SP on kernel stack
	 * safe-keeping not really needed, but it keeps the epilogue code
	 * (SP restore) simpler/uniform.
	 */
	b.d	66f
	mov	r9, sp

88: /*------Intr/Ecxp happened in user mode, "switch" stack ------ */

	GET_CURR_TASK_ON_CPU   r9

	/* With current tsk in r9, get it's kernel mode stack base */
	GET_TSK_STACK_BASE  r9, r9

66:
#ifdef CONFIG_ARC_CURR_IN_REG
	/*
	 * Treat r25 as scratch reg, save it on stack first
	 * Load it with current task pointer
	 */
	st	r25, [r9, -4]
	GET_CURR_TASK_ON_CPU   r25
#endif

	/* Save Pre Intr/Exception User SP on kernel stack */
	st.a    sp, [r9, -16]	; Make room for orig_r0, orig_r8, user_r25

	/* CAUTION:
	 * SP should be set at the very end when we are done with everything
	 * In case of 2 levels of interrupt we depend on value of SP to assume
	 * that everything else is done (loading r25 etc)
	 */

	/* set SP to point to kernel mode stack */
	mov sp, r9

	/* ----- Stack Switched to kernel Mode, Now save REG FILE ----- */

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
	bic \reg, sp, (THREAD_SIZE - 1)
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
#ifdef CONFIG_SMP
	sr  \reg, [ARC_REG_SCRATCH_DATA0]
#else
	st  \reg, [@ex_saved_reg1]
#endif
.endm

.macro EXCPN_PROLOG_RESTORE_REG	reg
#ifdef CONFIG_SMP
	lr  \reg, [ARC_REG_SCRATCH_DATA0]
#else
	ld  \reg, [@ex_saved_reg1]
#endif
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

	st      \marker, [sp, 8]	/* orig_r8 */
	st      r0, [sp, 4]    /* orig_r0, needed only for sys calls */

	/* Restore r9 used to code the early prologue */
	EXCPN_PROLOG_RESTORE_REG  r9

	SAVE_R0_TO_R12
	PUSH	gp
	PUSH	fp
	PUSH	blink
	PUSHAX	eret
	PUSHAX	erstatus
	PUSH	lp_count
	PUSHAX	lp_end
	PUSHAX	lp_start
	PUSHAX	erbta
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
	POPAX	erbta
	POPAX	lp_start
	POPAX	lp_end

	POP	r9
	mov	lp_count, r9	;LD to lp_count is not allowed

	POPAX	erstatus
	POPAX	eret
	POP	blink
	POP	fp
	POP	gp
	RESTORE_R12_TO_R0

	ld  sp, [sp] /* restore original sp */
	/* orig_r0, orig_r8, user_r25 skipped automatically */
.endm


/*--------------------------------------------------------------
 * Save all registers used by interrupt handlers.
 *-------------------------------------------------------------*/
.macro SAVE_ALL_INT1

	/* restore original r9 to be saved as part of reg-file */
#ifdef CONFIG_SMP
	lr  r9, [ARC_REG_SCRATCH_DATA0]
#else
	ld  r9, [@int1_saved_reg]
#endif

	/* now we are ready to save the remaining context :) */
	st      orig_r8_IS_IRQ1, [sp, 8]    /* Event Type */
	st      0, [sp, 4]    /* orig_r0 , N/A for IRQ */

	SAVE_R0_TO_R12
	PUSH	gp
	PUSH	fp
	PUSH	blink
	PUSH	ilink1
	PUSHAX	status32_l1
	PUSH	lp_count
	PUSHAX	lp_end
	PUSHAX	lp_start
	PUSHAX	bta_l1
.endm

.macro SAVE_ALL_INT2

	/* TODO-vineetg: SMP we can't use global nor can we use
	*   SCRATCH0 as we do for int1 because while int1 is using
	*   it, int2 can come
	*/
	/* retsore original r9 , saved in sys_saved_r9 */
	ld  r9, [@int2_saved_reg]

	/* now we are ready to save the remaining context :) */
	st      orig_r8_IS_IRQ2, [sp, 8]    /* Event Type */
	st      0, [sp, 4]    /* orig_r0 , N/A for IRQ */

	SAVE_R0_TO_R12
	PUSH	gp
	PUSH	fp
	PUSH	blink
	PUSH	ilink2
	PUSHAX	status32_l2
	PUSH	lp_count
	PUSHAX	lp_end
	PUSHAX	lp_start
	PUSHAX	bta_l2
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
	POPAX	bta_l1
	POPAX	lp_start
	POPAX	lp_end

	POP	r9
	mov	lp_count, r9	;LD to lp_count is not allowed

	POPAX	status32_l1
	POP	ilink1
	POP	blink
	POP	fp
	POP	gp
	RESTORE_R12_TO_R0

	ld  sp, [sp] /* restore original sp */
	/* orig_r0, orig_r8, user_r25 skipped automatically */
.endm

.macro RESTORE_ALL_INT2
	POPAX	bta_l2
	POPAX	lp_start
	POPAX	lp_end

	POP	r9
	mov	lp_count, r9	;LD to lp_count is not allowed

	POPAX	status32_l2
	POP	ilink2
	POP	blink
	POP	fp
	POP	gp
	RESTORE_R12_TO_R0

	ld  sp, [sp] /* restore original sp */
	/* orig_r0, orig_r8, user_r25 skipped automatically */
.endm


/* Get CPU-ID of this core */
.macro  GET_CPU_ID  reg
	lr  \reg, [identity]
	lsr \reg, \reg, 8
	bmsk \reg, \reg, 7
.endm

#ifdef CONFIG_SMP

/*-------------------------------------------------
 * Retrieve the current running task on this CPU
 * 1. Determine curr CPU id.
 * 2. Use it to index into _current_task[ ]
 */
.macro  GET_CURR_TASK_ON_CPU   reg
	GET_CPU_ID  \reg
	ld.as  \reg, [@_current_task, \reg]
.endm

/*-------------------------------------------------
 * Save a new task as the "current" task on this CPU
 * 1. Determine curr CPU id.
 * 2. Use it to index into _current_task[ ]
 *
 * Coded differently than GET_CURR_TASK_ON_CPU (which uses LD.AS)
 * because ST r0, [r1, offset] can ONLY have s9 @offset
 * while   LD can take s9 (4 byte insn) or LIMM (8 byte insn)
 */

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	GET_CPU_ID  \tmp
	add2 \tmp, @_current_task, \tmp
	st   \tsk, [\tmp]
#ifdef CONFIG_ARC_CURR_IN_REG
	mov r25, \tsk
#endif

.endm


#else   /* Uniprocessor implementation of macros */

.macro  GET_CURR_TASK_ON_CPU    reg
	ld  \reg, [@_current_task]
.endm

.macro  SET_CURR_TASK_ON_CPU    tsk, tmp
	st  \tsk, [@_current_task]
#ifdef CONFIG_ARC_CURR_IN_REG
	mov r25, \tsk
#endif
.endm

#endif /* SMP / UNI */

/* ------------------------------------------------------------------
 * Get the ptr to some field of Current Task at @off in task struct
 *  -Uses r25 for Current task ptr if that is enabled
 */

#ifdef CONFIG_ARC_CURR_IN_REG

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	add \reg, r25, \off
.endm

#else

.macro GET_CURR_TASK_FIELD_PTR  off,  reg
	GET_CURR_TASK_ON_CPU  \reg
	add \reg, \reg, \off
.endm

#endif	/* CONFIG_ARC_CURR_IN_REG */

#endif  /* __ASSEMBLY__ */

#endif  /* __ASM_ARC_ENTRY_H */
