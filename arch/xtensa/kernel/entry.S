/*
 * arch/xtensa/kernel/entry.S
 *
 * Low-level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2005 by Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <xtensa/coreasm.h>

/* Unimplemented features. */

#undef SIGNAL_HANDLING_IN_DOUBLE_EXCEPTION
#undef KERNEL_STACK_OVERFLOW_CHECK
#undef PREEMPTIBLE_KERNEL
#undef ALLOCA_EXCEPTION_IN_IRAM

/* Not well tested.
 *
 * - fast_coprocessor
 */

/*
 * Macro to find first bit set in WINDOWBASE from the left + 1
 *
 * 100....0 -> 1
 * 010....0 -> 2
 * 000....1 -> WSBITS
 */

	.macro ffs_ws bit mask

#if XCHAL_HAVE_NSA
	nsau    \bit, \mask			# 32-WSBITS ... 31 (32 iff 0)
	addi    \bit, \bit, WSBITS - 32 + 1   	# uppest bit set -> return 1
#else
	movi    \bit, WSBITS
#if WSBITS > 16
	_bltui  \mask, 0x10000, 99f
	addi    \bit, \bit, -16
	extui   \mask, \mask, 16, 16
#endif
#if WSBITS > 8
99:	_bltui  \mask, 0x100, 99f
	addi    \bit, \bit, -8
	srli    \mask, \mask, 8
#endif
99:	_bltui  \mask, 0x10, 99f
	addi    \bit, \bit, -4
	srli    \mask, \mask, 4
99:	_bltui  \mask, 0x4, 99f
	addi    \bit, \bit, -2
	srli    \mask, \mask, 2
99:	_bltui  \mask, 0x2, 99f
	addi    \bit, \bit, -1
99:

#endif
	.endm

/* ----------------- DEFAULT FIRST LEVEL EXCEPTION HANDLERS ----------------- */

/*
 * First-level exception handler for user exceptions.
 * Save some special registers, extra states and all registers in the AR
 * register file that were in use in the user task, and jump to the common
 * exception code.
 * We save SAR (used to calculate WMASK), and WB and WS (we don't have to
 * save them for kernel exceptions).
 *
 * Entry condition for user_exception:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original value in depc
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Entry condition for _user_exception:
 *
 *   a0-a3 and depc have been saved to PT_AREG0...PT_AREG3 and PT_DEPC
 *   excsave has been restored, and
 *   stack pointer (a1) has been set.
 *
 * Note: _user_exception might be at an odd adress. Don't use call0..call12
 */

ENTRY(user_exception)

	/* Save a2, a3, and depc, restore excsave_1 and set SP. */

	xsr	a3, EXCSAVE_1
	rsr	a0, DEPC
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	mov	a1, a2

	.globl _user_exception
_user_exception:

	/* Save SAR and turn off single stepping */

	movi	a2, 0
	rsr	a3, SAR
	wsr	a2, ICOUNTLEVEL
	s32i	a3, a1, PT_SAR

	/* Rotate ws so that the current windowbase is at bit0. */
	/* Assume ws = xxwww1yyyy. Rotate ws right, so that a2 = yyyyxxwww1 */

	rsr	a2, WINDOWBASE
	rsr	a3, WINDOWSTART
	ssr	a2
	s32i	a2, a1, PT_WINDOWBASE
	s32i	a3, a1, PT_WINDOWSTART
	slli	a2, a3, 32-WSBITS
	src	a2, a3, a2
	srli	a2, a2, 32-WSBITS
	s32i	a2, a1, PT_WMASK	# needed for restoring registers

	/* Save only live registers. */

	_bbsi.l	a2, 1, 1f
	s32i	a4, a1, PT_AREG4
	s32i	a5, a1, PT_AREG5
	s32i	a6, a1, PT_AREG6
	s32i	a7, a1, PT_AREG7
	_bbsi.l	a2, 2, 1f
	s32i	a8, a1, PT_AREG8
	s32i	a9, a1, PT_AREG9
	s32i	a10, a1, PT_AREG10
	s32i	a11, a1, PT_AREG11
	_bbsi.l	a2, 3, 1f
	s32i	a12, a1, PT_AREG12
	s32i	a13, a1, PT_AREG13
	s32i	a14, a1, PT_AREG14
	s32i	a15, a1, PT_AREG15
	_bnei	a2, 1, 1f		# only one valid frame?

	/* Only one valid frame, skip saving regs. */

	j	2f

	/* Save the remaining registers.
	 * We have to save all registers up to the first '1' from
	 * the right, except the current frame (bit 0).
	 * Assume a2 is:  001001000110001
	 * All regiser frames starting from the top fiel to the marked '1'
	 * must be saved.
	 */

1:	addi	a3, a2, -1		# eliminate '1' in bit 0: yyyyxxww0
	neg	a3, a3			# yyyyxxww0 -> YYYYXXWW1+1
	and	a3, a3, a2		# max. only one bit is set

	/* Find number of frames to save */

	ffs_ws	a0, a3			# number of frames to the '1' from left

	/* Store information into WMASK:
	 * bits 0..3: xxx1 masked lower 4 bits of the rotated windowstart,
	 * bits 4...: number of valid 4-register frames
	 */

	slli	a3, a0, 4		# number of frames to save in bits 8..4
	extui	a2, a2, 0, 4		# mask for the first 16 registers
	or	a2, a3, a2
	s32i	a2, a1, PT_WMASK	# needed when we restore the reg-file

	/* Save 4 registers at a time */

1:	rotw	-1
	s32i	a0, a5, PT_AREG_END - 16
	s32i	a1, a5, PT_AREG_END - 12
	s32i	a2, a5, PT_AREG_END - 8
	s32i	a3, a5, PT_AREG_END - 4
	addi	a0, a4, -1
	addi	a1, a5, -16
	_bnez	a0, 1b

	/* WINDOWBASE still in SAR! */

	rsr	a2, SAR			# original WINDOWBASE
	movi	a3, 1
	ssl	a2
	sll	a3, a3
	wsr	a3, WINDOWSTART		# set corresponding WINDOWSTART bit
	wsr	a2, WINDOWBASE		# and WINDOWSTART
	rsync

	/* We are back to the original stack pointer (a1) */

2:
#if XCHAL_EXTRA_SA_SIZE

	/* For user exceptions, save the extra state into the user's TCB.
	 * Note: We must assume that xchal_extra_store_funcbody destroys a2..a15
	 */

	GET_CURRENT(a2,a1)
	addi	a2, a2, THREAD_CP_SAVE
	xchal_extra_store_funcbody
#endif

	/* Now, jump to the common exception handler. */

	j	common_exception


/*
 * First-level exit handler for kernel exceptions
 * Save special registers and the live window frame.
 * Note: Even though we changes the stack pointer, we don't have to do a
 *	 MOVSP here, as we do that when we return from the exception.
 *	 (See comment in the kernel exception exit code)
 *
 * Entry condition for kernel_exception:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Entry condition for _kernel_exception:
 *
 *   a0-a3 and depc have been saved to PT_AREG0...PT_AREG3 and PT_DEPC
 *   excsave has been restored, and
 *   stack pointer (a1) has been set.
 *
 * Note: _kernel_exception might be at an odd adress. Don't use call0..call12
 */

ENTRY(kernel_exception)

	/* Save a0, a2, a3, DEPC and set SP. */

	xsr	a3, EXCSAVE_1		# restore a3, excsave_1
	rsr	a0, DEPC		# get a2
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	mov	a1, a2

	.globl _kernel_exception
_kernel_exception:

	/* Save SAR and turn off single stepping */

	movi	a2, 0
	rsr	a3, SAR
	wsr	a2, ICOUNTLEVEL
	s32i	a3, a1, PT_SAR

	/* Rotate ws so that the current windowbase is at bit0. */
	/* Assume ws = xxwww1yyyy. Rotate ws right, so that a2 = yyyyxxwww1 */

	rsr	a2, WINDOWBASE		# don't need to save these, we only
	rsr	a3, WINDOWSTART		# need shifted windowstart: windowmask
	ssr	a2
	slli	a2, a3, 32-WSBITS
	src	a2, a3, a2
	srli	a2, a2, 32-WSBITS
	s32i	a2, a1, PT_WMASK	# needed for kernel_exception_exit

	/* Save only the live window-frame */

	_bbsi.l	a2, 1, 1f
	s32i	a4, a1, PT_AREG4
	s32i	a5, a1, PT_AREG5
	s32i	a6, a1, PT_AREG6
	s32i	a7, a1, PT_AREG7
	_bbsi.l	a2, 2, 1f
	s32i	a8, a1, PT_AREG8
	s32i	a9, a1, PT_AREG9
	s32i	a10, a1, PT_AREG10
	s32i	a11, a1, PT_AREG11
	_bbsi.l	a2, 3, 1f
	s32i	a12, a1, PT_AREG12
	s32i	a13, a1, PT_AREG13
	s32i	a14, a1, PT_AREG14
	s32i	a15, a1, PT_AREG15

1:

#ifdef KERNEL_STACK_OVERFLOW_CHECK

	/*  Stack overflow check, for debugging  */
	extui	a2, a1, TASK_SIZE_BITS,XX
	movi	a3, SIZE??
	_bge	a2, a3, out_of_stack_panic

#endif

/*
 * This is the common exception handler.
 * We get here from the user exception handler or simply by falling through
 * from the kernel exception handler.
 * Save the remaining special registers, switch to kernel mode, and jump
 * to the second-level exception handler.
 *
 */

common_exception:

	/* Save EXCVADDR, DEBUGCAUSE, and PC, and clear LCOUNT */

	rsr	a2, DEBUGCAUSE
	rsr	a3, EPC_1
	s32i	a2, a1, PT_DEBUGCAUSE
	s32i	a3, a1, PT_PC

	rsr	a3, EXCVADDR
	movi	a2, 0
	s32i	a3, a1, PT_EXCVADDR
	xsr	a2, LCOUNT
	s32i	a2, a1, PT_LCOUNT

	/* It is now save to restore the EXC_TABLE_FIXUP variable. */

	rsr	a0, EXCCAUSE
	movi	a3, 0
	rsr	a2, EXCSAVE_1
	s32i	a0, a1, PT_EXCCAUSE
	s32i	a3, a2, EXC_TABLE_FIXUP

	/* All unrecoverable states are saved on stack, now, and a1 is valid,
	 * so we can allow exceptions and interrupts (*) again.
	 * Set PS(EXCM = 0, UM = 0, RING = 0, OWB = 0, WOE = 1, INTLEVEL = X)
	 *
	 * (*) We only allow interrupts if PS.INTLEVEL was not set to 1 before
	 *     (interrupts disabled) and if this exception is not an interrupt.
	 */

	rsr	a3, PS
	addi	a0, a0, -4
	movi	a2, 1
	extui	a3, a3, 0, 1		# a3 = PS.INTLEVEL[0]
	moveqz	a3, a2, a0		# a3 = 1 iff interrupt exception
	movi	a2, PS_WOE_MASK
	or	a3, a3, a2
	rsr	a0, EXCCAUSE
	xsr	a3, PS

	s32i	a3, a1, PT_PS		# save ps

	/* Save LBEG, LEND */

	rsr	a2, LBEG
	rsr	a3, LEND
	s32i	a2, a1, PT_LBEG
	s32i	a3, a1, PT_LEND

	/* Go to second-level dispatcher. Set up parameters to pass to the
	 * exception handler and call the exception handler.
	 */

	movi	a4, exc_table
	mov	a6, a1			# pass stack frame
	mov	a7, a0			# pass EXCCAUSE
	addx4	a4, a0, a4
	l32i	a4, a4, EXC_TABLE_DEFAULT		# load handler

	/* Call the second-level handler */

	callx4	a4

	/* Jump here for exception exit */

common_exception_return:

	/* Jump if we are returning from kernel exceptions. */

1:	l32i	a3, a1, PT_PS
	_bbsi.l	a3, PS_UM_SHIFT, 2f
	j	kernel_exception_exit

	/* Specific to a user exception exit:
	 * We need to check some flags for signal handling and rescheduling,
	 * and have to restore WB and WS, extra states, and all registers
	 * in the register file that were in use in the user task.
	 */

2:	wsr	a3, PS		/* disable interrupts */

	/* Check for signals (keep interrupts disabled while we read TI_FLAGS)
	 * Note: PS.INTLEVEL = 0, PS.EXCM = 1
	 */

	GET_THREAD_INFO(a2,a1)
	l32i	a4, a2, TI_FLAGS

	/* Enable interrupts again.
	 * Note: When we get here, we certainly have handled any interrupts.
	 *       (Hint: There is only one user exception frame on stack)
	 */

	movi	a3, PS_WOE_MASK

	_bbsi.l	a4, TIF_NEED_RESCHED, 3f
	_bbci.l	a4, TIF_SIGPENDING, 4f

#ifndef SIGNAL_HANDLING_IN_DOUBLE_EXCEPTION
	l32i	a4, a1, PT_DEPC
	bgeui	a4, VALID_DOUBLE_EXCEPTION_ADDRESS, 4f
#endif

	/* Reenable interrupts and call do_signal() */

	wsr	a3, PS
	movi	a4, do_signal	# int do_signal(struct pt_regs*, sigset_t*)
	mov	a6, a1
	movi	a7, 0
	callx4	a4
	j	1b

3:	/* Reenable interrupts and reschedule */

	wsr	a3, PS
	movi	a4, schedule	# void schedule (void)
	callx4	a4
	j	1b

	/* Restore the state of the task and return from the exception. */


	/* If we are returning from a user exception, and the process
	 * to run next has PT_SINGLESTEP set, we want to setup
	 * ICOUNT and ICOUNTLEVEL to step one instruction.
	 * PT_SINGLESTEP is set by sys_ptrace (ptrace.c)
	 */

4:	/* a2 holds GET_CURRENT(a2,a1)  */

	l32i	a3, a2, TI_TASK
	l32i	a3, a3, TASK_PTRACE
	bbci.l	a3, PT_SINGLESTEP_BIT, 1f # jump if single-step flag is not set

	movi	a3, -2			# PT_SINGLESTEP flag is set,
	movi	a4, 1			# icountlevel of 1 means it won't
	wsr	a3, ICOUNT		# start counting until after rfe
	wsr	a4, ICOUNTLEVEL		# so setup icount & icountlevel.
	isync

1:

#if XCHAL_EXTRA_SA_SIZE

	/* For user exceptions, restore the extra state from the user's TCB. */

	/* Note: a2 still contains GET_CURRENT(a2,a1) */
	addi	a2, a2, THREAD_CP_SAVE
	xchal_extra_load_funcbody

	/* We must assume that xchal_extra_store_funcbody destroys
	 * registers a2..a15.  FIXME, this list can eventually be
	 * reduced once real register requirements of the macro are
	 * finalized. */

#endif /* XCHAL_EXTRA_SA_SIZE */


	/* Switch to the user thread WINDOWBASE. Save SP temporarily in DEPC */

	l32i	a2, a1, PT_WINDOWBASE
	l32i	a3, a1, PT_WINDOWSTART
	wsr	a1, DEPC		# use DEPC as temp storage
	wsr	a3, WINDOWSTART		# restore WINDOWSTART
	ssr	a2			# preserve user's WB in the SAR
	wsr	a2, WINDOWBASE		# switch to user's saved WB
	rsync
	rsr	a1, DEPC		# restore stack pointer
	l32i	a2, a1, PT_WMASK	# register frames saved (in bits 4...9)
	rotw	-1			# we restore a4..a7
	_bltui	a6, 16, 1f		# only have to restore current window?

	/* The working registers are a0 and a3.  We are restoring to
	 * a4..a7.  Be careful not to destroy what we have just restored.
	 * Note: wmask has the format YYYYM:
	 *       Y: number of registers saved in groups of 4
	 *       M: 4 bit mask of first 16 registers
	 */

	mov	a2, a6
	mov	a3, a5

2:	rotw	-1			# a0..a3 become a4..a7
	addi	a3, a7, -4*4		# next iteration
	addi	a2, a6, -16		# decrementing Y in WMASK
	l32i	a4, a3, PT_AREG_END + 0
	l32i	a5, a3, PT_AREG_END + 4
	l32i	a6, a3, PT_AREG_END + 8
	l32i	a7, a3, PT_AREG_END + 12
	_bgeui	a2, 16, 2b

	/* Clear unrestored registers (don't leak anything to user-land */

1:	rsr	a0, WINDOWBASE
	rsr	a3, SAR
	sub	a3, a0, a3
	beqz	a3, 2f
	extui	a3, a3, 0, WBBITS

1:	rotw	-1
	addi	a3, a7, -1
	movi	a4, 0
	movi	a5, 0
	movi	a6, 0
	movi	a7, 0
	bgei	a3, 1, 1b

	/* We are back were we were when we started.
	 * Note: a2 still contains WMASK (if we've returned to the original
	 *	 frame where we had loaded a2), or at least the lower 4 bits
	 *	 (if we have restored WSBITS-1 frames).
	 */

2:	j	common_exception_exit

	/* This is the kernel exception exit.
	 * We avoided to do a MOVSP when we entered the exception, but we
	 * have to do it here.
	 */

kernel_exception_exit:

	/* Disable interrupts (a3 holds PT_PS) */

	wsr	a3, PS

#ifdef PREEMPTIBLE_KERNEL

#ifdef CONFIG_PREEMPT

	/*
	 * Note: We've just returned from a call4, so we have
	 * at least 4 addt'l regs.
	 */

	/* Check current_thread_info->preempt_count */

	GET_THREAD_INFO(a2)
	l32i	a3, a2, TI_PREEMPT
	bnez	a3, 1f

	l32i	a2, a2, TI_FLAGS

1:

#endif

#endif

	/* Check if we have to do a movsp.
	 *
	 * We only have to do a movsp if the previous window-frame has
	 * been spilled to the *temporary* exception stack instead of the
	 * task's stack. This is the case if the corresponding bit in
	 * WINDOWSTART for the previous window-frame was set before
	 * (not spilled) but is zero now (spilled).
	 * If this bit is zero, all other bits except the one for the
	 * current window frame are also zero. So, we can use a simple test:
	 * 'and' WINDOWSTART and WINDOWSTART-1:
	 *
	 *  (XXXXXX1[0]* - 1) AND XXXXXX1[0]* = XXXXXX0[0]*
	 *
	 * The result is zero only if one bit was set.
	 *
	 * (Note: We might have gone through several task switches before
	 *        we come back to the current task, so WINDOWBASE might be
	 *        different from the time the exception occurred.)
	 */

	/* Test WINDOWSTART before and after the exception.
	 * We actually have WMASK, so we only have to test if it is 1 or not.
	 */

	l32i	a2, a1, PT_WMASK
	_beqi	a2, 1, common_exception_exit	# Spilled before exception,jump

	/* Test WINDOWSTART now. If spilled, do the movsp */

	rsr     a3, WINDOWSTART
	addi	a0, a3, -1
	and     a3, a3, a0
	_bnez	a3, common_exception_exit

	/* Do a movsp (we returned from a call4, so we have at least a0..a7) */

	addi    a0, a1, -16
	l32i    a3, a0, 0
	l32i    a4, a0, 4
	s32i    a3, a1, PT_SIZE+0
	s32i    a4, a1, PT_SIZE+4
	l32i    a3, a0, 8
	l32i    a4, a0, 12
	s32i    a3, a1, PT_SIZE+8
	s32i    a4, a1, PT_SIZE+12

	/* Common exception exit.
	 * We restore the special register and the current window frame, and
	 * return from the exception.
	 *
	 * Note: We expect a2 to hold PT_WMASK
	 */

common_exception_exit:

	_bbsi.l	a2, 1, 1f
	l32i	a4,  a1, PT_AREG4
	l32i	a5,  a1, PT_AREG5
	l32i	a6,  a1, PT_AREG6
	l32i	a7,  a1, PT_AREG7
	_bbsi.l	a2, 2, 1f
	l32i	a8,  a1, PT_AREG8
	l32i	a9,  a1, PT_AREG9
	l32i	a10, a1, PT_AREG10
	l32i	a11, a1, PT_AREG11
	_bbsi.l	a2, 3, 1f
	l32i	a12, a1, PT_AREG12
	l32i	a13, a1, PT_AREG13
	l32i	a14, a1, PT_AREG14
	l32i	a15, a1, PT_AREG15

	/* Restore PC, SAR */

1:	l32i	a2, a1, PT_PC
	l32i	a3, a1, PT_SAR
	wsr	a2, EPC_1
	wsr	a3, SAR

	/* Restore LBEG, LEND, LCOUNT */

	l32i	a2, a1, PT_LBEG
	l32i	a3, a1, PT_LEND
	wsr	a2, LBEG
	l32i	a2, a1, PT_LCOUNT
	wsr	a3, LEND
	wsr	a2, LCOUNT

	/* Check if it was double exception. */

	l32i	a0, a1, PT_DEPC
	l32i	a3, a1, PT_AREG3
	l32i	a2, a1, PT_AREG2
	_bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	/* Restore a0...a3 and return */

	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1
	rfe

1:	wsr	a0, DEPC
	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1
	rfde

/*
 * Debug exception handler.
 *
 * Currently, we don't support KGDB, so only user application can be debugged.
 *
 * When we get here,  a0 is trashed and saved to excsave[debuglevel]
 */

ENTRY(debug_exception)

	rsr	a0, EPS + XCHAL_DEBUGLEVEL
	bbsi.l	a0, PS_EXCM_SHIFT, 1f	# exception mode

	/* Set EPC_1 and EXCCAUSE */

	wsr	a2, DEPC		# save a2 temporarily
	rsr	a2, EPC + XCHAL_DEBUGLEVEL
	wsr	a2, EPC_1

	movi	a2, EXCCAUSE_MAPPED_DEBUG
	wsr	a2, EXCCAUSE

	/* Restore PS to the value before the debug exc but with PS.EXCM set.*/

	movi	a2, 1 << PS_EXCM_SHIFT
	or	a2, a0, a2
	movi	a0, debug_exception	# restore a3, debug jump vector
	wsr	a2, PS
	xsr	a0, EXCSAVE + XCHAL_DEBUGLEVEL

	/* Switch to kernel/user stack, restore jump vector, and save a0 */

	bbsi.l	a2, PS_UM_SHIFT, 2f	# jump if user mode

	addi	a2, a1, -16-PT_SIZE	# assume kernel stack
	s32i	a0, a2, PT_AREG0
	movi	a0, 0
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	xsr	a0, DEPC
	s32i	a3, a2, PT_AREG3
	s32i	a0, a2, PT_AREG2
	mov	a1, a2
	j	_kernel_exception

2:	rsr	a2, EXCSAVE_1
	l32i	a2, a2, EXC_TABLE_KSTK	# load kernel stack pointer
	s32i	a0, a2, PT_AREG0
	movi	a0, 0
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_DEPC
	xsr	a0, DEPC
	s32i	a3, a2, PT_AREG3
	s32i	a0, a2, PT_AREG2
	mov	a1, a2
	j	_user_exception

	/* Debug exception while in exception mode. */
1:	j	1b	// FIXME!!


/*
 * We get here in case of an unrecoverable exception.
 * The only thing we can do is to be nice and print a panic message.
 * We only produce a single stack frame for panic, so ???
 *
 *
 * Entry conditions:
 *
 *   - a0 contains the caller address; original value saved in excsave1.
 *   - the original a0 contains a valid return address (backtrace) or 0.
 *   - a2 contains a valid stackpointer
 *
 * Notes:
 *
 *   - If the stack pointer could be invalid, the caller has to setup a
 *     dummy stack pointer (e.g. the stack of the init_task)
 *
 *   - If the return address could be invalid, the caller has to set it
 *     to 0, so the backtrace would stop.
 *
 */
	.align 4
unrecoverable_text:
	.ascii "Unrecoverable error in exception handler\0"

ENTRY(unrecoverable_exception)

	movi	a0, 1
	movi	a1, 0

	wsr	a0, WINDOWSTART
	wsr	a1, WINDOWBASE
	rsync

	movi	a1, PS_WOE_MASK | 1
	wsr	a1, PS
	rsync

	movi	a1, init_task
	movi	a0, 0
	addi	a1, a1, PT_REGS_OFFSET

	movi	a4, panic
	movi	a6, unrecoverable_text

	callx4	a4

1:	j	1b


/* -------------------------- FAST EXCEPTION HANDLERS ----------------------- */

/*
 * Fast-handler for alloca exceptions
 *
 *  The ALLOCA handler is entered when user code executes the MOVSP
 *  instruction and the caller's frame is not in the register file.
 *  In this case, the caller frame's a0..a3 are on the stack just
 *  below sp (a1), and this handler moves them.
 *
 *  For "MOVSP <ar>,<as>" without destination register a1, this routine
 *  simply moves the value from <as> to <ar> without moving the save area.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

#if XCHAL_HAVE_BE
#define _EXTUI_MOVSP_SRC(ar)	extui ar, ar, 4, 4
#define _EXTUI_MOVSP_DST(ar)	extui ar, ar, 0, 4
#else
#define _EXTUI_MOVSP_SRC(ar)	extui ar, ar, 0, 4
#define _EXTUI_MOVSP_DST(ar)	extui ar, ar, 4, 4
#endif

ENTRY(fast_alloca)

	/* We shouldn't be in a double exception. */

	l32i	a0, a2, PT_DEPC
	_bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, .Lunhandled_double

	rsr	a0, DEPC		# get a2
	s32i	a4, a2, PT_AREG4	# save a4 and
	s32i	a0, a2, PT_AREG2	# a2 to stack

	/* Exit critical section. */

	movi	a0, 0
	s32i	a0, a3, EXC_TABLE_FIXUP

	/* Restore a3, excsave_1 */

	xsr	a3, EXCSAVE_1		# make sure excsave_1 is valid for dbl.
	rsr	a4, EPC_1		# get exception address
	s32i	a3, a2, PT_AREG3	# save a3 to stack

#ifdef ALLOCA_EXCEPTION_IN_IRAM
#error	iram not supported
#else
	/* Note: l8ui not allowed in IRAM/IROM!! */
	l8ui	a0, a4, 1		# read as(src) from MOVSP instruction
#endif
	movi	a3, .Lmovsp_src
	_EXTUI_MOVSP_SRC(a0)		# extract source register number
	addx8	a3, a0, a3
	jx	a3

.Lunhandled_double:
	wsr	a0, EXCSAVE_1
	movi	a0, unrecoverable_exception
	callx0	a0

	.align 8
.Lmovsp_src:
	l32i	a3, a2, PT_AREG0;	_j 1f;	.align 8
	mov	a3, a1;			_j 1f;	.align 8
	l32i	a3, a2, PT_AREG2;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG3;	_j 1f;	.align 8
	l32i	a3, a2, PT_AREG4;	_j 1f;	.align 8
	mov	a3, a5;			_j 1f;	.align 8
	mov	a3, a6;			_j 1f;	.align 8
	mov	a3, a7;			_j 1f;	.align 8
	mov	a3, a8;			_j 1f;	.align 8
	mov	a3, a9;			_j 1f;	.align 8
	mov	a3, a10;		_j 1f;	.align 8
	mov	a3, a11;		_j 1f;	.align 8
	mov	a3, a12;		_j 1f;	.align 8
	mov	a3, a13;		_j 1f;	.align 8
	mov	a3, a14;		_j 1f;	.align 8
	mov	a3, a15;		_j 1f;	.align 8

1:

#ifdef ALLOCA_EXCEPTION_IN_IRAM
#error	iram not supported
#else
	l8ui	a0, a4, 0		# read ar(dst) from MOVSP instruction
#endif
	addi	a4, a4, 3		# step over movsp
	_EXTUI_MOVSP_DST(a0)		# extract destination register
	wsr	a4, EPC_1		# save new epc_1

	_bnei	a0, 1, 1f		# no 'movsp a1, ax': jump

        /* Move the save area. This implies the use of the L32E
	 * and S32E instructions, because this move must be done with
	 * the user's PS.RING privilege levels, not with ring 0
	 * (kernel's) privileges currently active with PS.EXCM
	 * set. Note that we have stil registered a fixup routine with the
	 * double exception vector in case a double exception occurs.
	 */

	/* a0,a4:avail a1:old user stack a2:exc. stack a3:new user stack. */

	l32e	a0, a1, -16
	l32e	a4, a1, -12
	s32e	a0, a3, -16
	s32e	a4, a3, -12
	l32e	a0, a1, -8
	l32e	a4, a1, -4
	s32e	a0, a3, -8
	s32e	a4, a3, -4

	/* Restore stack-pointer and all the other saved registers. */

	mov	a1, a3

	l32i	a4, a2, PT_AREG4
	l32i	a3, a2, PT_AREG3
	l32i	a0, a2, PT_AREG0
	l32i	a2, a2, PT_AREG2
	rfe

	/*  MOVSP <at>,<as>  was invoked with <at> != a1.
	 *  Because the stack pointer is not being modified,
	 *  we should be able to just modify the pointer
	 *  without moving any save area.
	 *  The processor only traps these occurrences if the
	 *  caller window isn't live, so unfortunately we can't
	 *  use this as an alternate trap mechanism.
	 *  So we just do the move.  This requires that we
	 *  resolve the destination register, not just the source,
	 *  so there's some extra work.
	 *  (PERHAPS NOT REALLY NEEDED, BUT CLEANER...)
	 */

	/* a0 dst-reg, a1 user-stack, a2 stack, a3 value of src reg. */

1:	movi	a4, .Lmovsp_dst
	addx8	a4, a0, a4
	jx	a4

	.align 8
.Lmovsp_dst:
	s32i	a3, a2, PT_AREG0;	_j 1f;	.align 8
	mov	a1, a3;			_j 1f;	.align 8
	s32i	a3, a2, PT_AREG2;	_j 1f;	.align 8
	s32i	a3, a2, PT_AREG3;	_j 1f;	.align 8
	s32i	a3, a2, PT_AREG4;	_j 1f;	.align 8
	mov	a5, a3;			_j 1f;	.align 8
	mov	a6, a3;			_j 1f;	.align 8
	mov	a7, a3;			_j 1f;	.align 8
	mov	a8, a3;			_j 1f;	.align 8
	mov	a9, a3;			_j 1f;	.align 8
	mov	a10, a3;		_j 1f;	.align 8
	mov	a11, a3;		_j 1f;	.align 8
	mov	a12, a3;		_j 1f;	.align 8
	mov	a13, a3;		_j 1f;	.align 8
	mov	a14, a3;		_j 1f;	.align 8
	mov	a15, a3;		_j 1f;	.align 8

1:	l32i	a4, a2, PT_AREG4
	l32i	a3, a2, PT_AREG3
	l32i	a0, a2, PT_AREG0
	l32i	a2, a2, PT_AREG2
	rfe


/*
 * fast system calls.
 *
 * WARNING:  The kernel doesn't save the entire user context before
 * handling a fast system call.  These functions are small and short,
 * usually offering some functionality not available to user tasks.
 *
 * BE CAREFUL TO PRESERVE THE USER'S CONTEXT.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 */

ENTRY(fast_syscall_kernel)

	/* Skip syscall. */

	rsr	a0, EPC_1
	addi	a0, a0, 3
	wsr	a0, EPC_1

	l32i	a0, a2, PT_DEPC
	bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, fast_syscall_unrecoverable

	rsr	a0, DEPC			# get syscall-nr
	_beqz	a0, fast_syscall_spill_registers

	addi	a0, a0, -__NR_sysxtensa
	_beqz	a0, fast_syscall_sysxtensa

	j	kernel_exception


ENTRY(fast_syscall_user)

	/* Skip syscall. */

	rsr	a0, EPC_1
	addi	a0, a0, 3
	wsr	a0, EPC_1

	l32i	a0, a2, PT_DEPC
	bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, fast_syscall_unrecoverable

	rsr	a0, DEPC			# get syscall-nr
	_beqz	a0, fast_syscall_spill_registers

	addi	a0, a0, -__NR_sysxtensa
	_beqz	a0, fast_syscall_sysxtensa

	j	user_exception

ENTRY(fast_syscall_unrecoverable)

        /* Restore all states. */

        l32i    a0, a2, PT_AREG0        # restore a0
        xsr     a2, DEPC                # restore a2, depc
        rsr     a3, EXCSAVE_1

        wsr     a0, EXCSAVE_1
        movi    a0, unrecoverable_exception
        callx0  a0



/*
 * sysxtensa syscall handler
 *
 * int sysxtensa (XTENSA_ATOMIC_SET, ptr, val, unused);
 * int sysxtensa (XTENSA_ATOMIC_ADD, ptr, val, unused);
 * int sysxtensa (XTENSA_ATOMIC_EXG_ADD, ptr, val, unused);
 * int sysxtensa (XTENSA_ATOMIC_CMP_SWP, ptr, oldval, newval);
 * a2                    a6              a3    a4      a5
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Note: we don't have to save a2; a2 holds the return value
 *
 * We use the two macros TRY and CATCH:
 *
 * TRY	 adds an entry to the __ex_table fixup table for the immediately
 *	 following instruction.
 *
 * CATCH catches any exception that occurred at one of the preceeding TRY
 *       statements and continues from there
 *
 * Usage TRY	l32i	a0, a1, 0
 *		<other code>
 *	 done:	rfe
 *	 CATCH	<set return code>
 *		j done
 */

#define TRY								\
	.section __ex_table, "a";					\
	.word	66f, 67f;						\
	.text;								\
66:

#define CATCH								\
67:

ENTRY(fast_syscall_sysxtensa)

	_beqz	a6, 1f
	_blti	a6, SYSXTENSA_COUNT, 2f

1:	j	user_exception

2:	xsr	a3, EXCSAVE_1		# restore a3, excsave1
	s32i	a7, a2, PT_AREG7

	movi	a7, 4			# sizeof(unsigned int)
	access_ok a0, a3, a7, a2, .Leac

	_beqi	a6, SYSXTENSA_ATOMIC_SET, .Lset
	_beqi	a6, SYSXTENSA_ATOMIC_EXG_ADD, .Lexg
	_beqi	a6, SYSXTENSA_ATOMIC_ADD, .Ladd

	/* Fall through for SYSXTENSA_ATOMIC_CMP_SWP */

.Lswp:	/* Atomic compare and swap */

TRY	l32i	a7, a3, 0		# read old value
	bne	a7, a4, 1f		# same as old value? jump
	s32i	a5, a3, 0		# different, modify value
	movi	a7, 1			# and return 1
	j	.Lret

1:	movi	a7, 0			# same values: return 0
	j	.Lret

.Ladd:	/* Atomic add */
.Lexg:	/* Atomic (exchange) add */

TRY	l32i	a7, a3, 0
	add	a4, a4, a7
	s32i	a4, a3, 0
	j	.Lret

.Lset:	/* Atomic set */

TRY	l32i	a7, a3, 0		# read old value as return value
	s32i	a4, a3, 0		# write new value

.Lret:	mov	a0, a2
	mov	a2, a7
	l32i	a7, a0, PT_AREG7
	l32i	a3, a0, PT_AREG3
	l32i	a0, a0, PT_AREG0
	rfe

CATCH
.Leac:	movi	a7, -EFAULT
	j	.Lret



/* fast_syscall_spill_registers.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 * Note: We assume the stack pointer is EXC_TABLE_KSTK in the fixup handler.
 * Note: We don't need to save a2 in depc (return value)
 */

ENTRY(fast_syscall_spill_registers)

	/* Register a FIXUP handler (pass current wb as a parameter) */

	movi	a0, fast_syscall_spill_registers_fixup
	s32i	a0, a3, EXC_TABLE_FIXUP
	rsr	a0, WINDOWBASE
	s32i	a0, a3, EXC_TABLE_PARAM

	/* Save a3 and SAR on stack. */

	rsr	a0, SAR
	xsr	a3, EXCSAVE_1		# restore a3 and excsave_1
	s32i	a0, a2, PT_AREG4	# store SAR to PT_AREG4
	s32i	a3, a2, PT_AREG3

	/* The spill routine might clobber a7, a11, and a15. */

	s32i	a7, a2, PT_AREG5
	s32i	a11, a2, PT_AREG6
	s32i	a15, a2, PT_AREG7

	call0	_spill_registers	# destroys a3, DEPC, and SAR

	/* Advance PC, restore registers and SAR, and return from exception. */

	l32i	a3, a2, PT_AREG4
	l32i	a0, a2, PT_AREG0
	wsr	a3, SAR
	l32i	a3, a2, PT_AREG3

	/* Restore clobbered registers. */

	l32i	a7, a2, PT_AREG5
	l32i	a11, a2, PT_AREG6
	l32i	a15, a2, PT_AREG7

	movi	a2, 0
	rfe

/* Fixup handler.
 *
 * We get here if the spill routine causes an exception, e.g. tlb miss.
 * We basically restore WINDOWBASE and WINDOWSTART to the condition when
 * we entered the spill routine and jump to the user exception handler.
 *
 * a0: value of depc, original value in depc
 * a2: trashed, original value in EXC_TABLE_DOUBLE_SAVE
 * a3: exctable, original value in excsave1
 */

fast_syscall_spill_registers_fixup:

	rsr	a2, WINDOWBASE	# get current windowbase (a2 is saved)
	xsr	a0, DEPC	# restore depc and a0
	ssl	a2		# set shift (32 - WB)

	/* We need to make sure the current registers (a0-a3) are preserved.
	 * To do this, we simply set the bit for the current window frame
	 * in WS, so that the exception handlers save them to the task stack.
	 */

	rsr	a3, EXCSAVE_1	# get spill-mask
	slli	a2, a3, 1	# shift left by one

	slli	a3, a2, 32-WSBITS
	src	a2, a2, a3	# a1 = xxwww1yyxxxwww1yy......
	wsr	a2, WINDOWSTART	# set corrected windowstart

	movi	a3, exc_table
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE	# restore a2
	l32i	a3, a3, EXC_TABLE_PARAM	# original WB (in user task)

	/* Return to the original (user task) WINDOWBASE.
	 * We leave the following frame behind:
	 * a0, a1, a2	same
	 * a3:		trashed (saved in excsave_1)
	 * depc:	depc (we have to return to that address)
	 * excsave_1:	a3
	 */

	wsr	a3, WINDOWBASE
	rsync

	/* We are now in the original frame when we entered _spill_registers:
	 *  a0: return address
	 *  a1: used, stack pointer
	 *  a2: kernel stack pointer
	 *  a3: available, saved in EXCSAVE_1
	 *  depc: exception address
	 *  excsave: a3
	 * Note: This frame might be the same as above.
	 */

#ifdef SIGNAL_HANDLING_IN_DOUBLE_EXCEPTION
	/* Restore registers we precautiously saved.
	 * We have the value of the 'right' a3
	 */

	l32i	a7, a2, PT_AREG5
	l32i	a11, a2, PT_AREG6
	l32i	a15, a2, PT_AREG7
#endif

	/* Setup stack pointer. */

	addi	a2, a2, -PT_USER_SIZE
	s32i	a0, a2, PT_AREG0

	/* Make sure we return to this fixup handler. */

	movi	a3, fast_syscall_spill_registers_fixup_return
	s32i	a3, a2, PT_DEPC		# setup depc

	/* Jump to the exception handler. */

	movi	a3, exc_table
	rsr	a0, EXCCAUSE
        addx4   a0, a0, a3              	# find entry in table
        l32i    a0, a0, EXC_TABLE_FAST_USER     # load handler
        jx      a0

fast_syscall_spill_registers_fixup_return:

	/* When we return here, all registers have been restored (a2: DEPC) */

	wsr	a2, DEPC		# exception address

	/* Restore fixup handler. */

	xsr	a3, EXCSAVE_1
	movi	a2, fast_syscall_spill_registers_fixup
	s32i	a2, a3, EXC_TABLE_FIXUP
	rsr	a2, WINDOWBASE
	s32i	a2, a3, EXC_TABLE_PARAM
	l32i	a2, a3, EXC_TABLE_KSTK

#ifdef SIGNAL_HANDLING_IN_DOUBLE_EXCEPTION
	/* Save registers again that might be clobbered. */

	s32i	a7, a2, PT_AREG5
	s32i	a11, a2, PT_AREG6
	s32i	a15, a2, PT_AREG7
#endif

	/* Load WB at the time the exception occurred. */

	rsr	a3, SAR			# WB is still in SAR
	neg	a3, a3
	wsr	a3, WINDOWBASE
	rsync

	/* Restore a3 and return. */

	movi	a3, exc_table
	xsr	a3, EXCSAVE_1

	rfde


/*
 * spill all registers.
 *
 * This is not a real function. The following conditions must be met:
 *
 *  - must be called with call0.
 *  - uses DEPC, a3 and SAR.
 *  - the last 'valid' register of each frame are clobbered.
 *  - the caller must have registered a fixup handler
 *    (or be inside a critical section)
 *  - PS_EXCM must be set (PS_WOE cleared?)
 */

ENTRY(_spill_registers)

	/*
	 * Rotate ws so that the current windowbase is at bit 0.
	 * Assume ws = xxxwww1yy (www1 current window frame).
	 * Rotate ws right so that a2 = yyxxxwww1.
	 */

	wsr	a2, DEPC		# preserve a2
	rsr	a2, WINDOWBASE
	rsr	a3, WINDOWSTART
	ssr	a2			# holds WB
	slli	a2, a3, WSBITS
	or	a3, a3, a2		# a2 = xxxwww1yyxxxwww1yy
	srl	a3, a3

	/* We are done if there are no more than the current register frame. */

	extui	a3, a3, 1, WSBITS-2	# a3 = 0yyxxxwww
	movi	a2, (1 << (WSBITS-1))
	_beqz	a3, .Lnospill		# only one active frame? jump

	/* We want 1 at the top, so that we return to the current windowbase */

	or	a3, a3, a2		# 1yyxxxwww

	/* Skip empty frames - get 'oldest' WINDOWSTART-bit. */

	wsr	a3, WINDOWSTART		# save shifted windowstart
	neg	a2, a3
	and	a3, a2, a3		# first bit set from right: 000010000

	ffs_ws	a2, a3			# a2: shifts to skip empty frames
	movi	a3, WSBITS
	sub	a2, a3, a2		# WSBITS-a2:number of 0-bits from right
	ssr	a2			# save in SAR for later.

	rsr	a3, WINDOWBASE
	add	a3, a3, a2
	rsr	a2, DEPC		# restore a2
	wsr	a3, WINDOWBASE
	rsync

	rsr	a3, WINDOWSTART
	srl	a3, a3			# shift windowstart

	/* WB is now just one frame below the oldest frame in the register
	   window. WS is shifted so the oldest frame is in bit 0, thus, WB
	   and WS differ by one 4-register frame. */

	/* Save frames. Depending what call was used (call4, call8, call12),
	 * we have to save 4,8. or 12 registers.
	 */

	_bbsi.l	a3, 1, .Lc4
	_bbsi.l	a3, 2, .Lc8

	/* Special case: we have a call12-frame starting at a4. */

	_bbci.l	a3, 3, .Lc12	# bit 3 shouldn't be zero! (Jump to Lc12 first)

	s32e	a4, a1, -16	# a1 is valid with an empty spill area
	l32e	a4, a5, -12
	s32e	a8, a4, -48
	mov	a8, a4
	l32e	a4, a1, -16
	j	.Lc12c

.Lloop: _bbsi.l	a3, 1, .Lc4
	_bbci.l	a3, 2, .Lc12

.Lc8:	s32e	a4, a13, -16
	l32e	a4, a5, -12
	s32e	a8, a4, -32
	s32e	a5, a13, -12
	s32e	a6, a13, -8
	s32e	a7, a13, -4
	s32e	a9, a4, -28
	s32e	a10, a4, -24
	s32e	a11, a4, -20

	srli	a11, a3, 2		# shift windowbase by 2
	rotw	2
	_bnei	a3, 1, .Lloop

.Lexit: /* Done. Do the final rotation, set WS, and return. */

	rotw	1
	rsr	a3, WINDOWBASE
	ssl	a3
	movi	a3, 1
	sll	a3, a3
	wsr	a3, WINDOWSTART

.Lnospill:
	jx	a0

.Lc4:	s32e	a4, a9, -16
	s32e	a5, a9, -12
	s32e	a6, a9, -8
	s32e	a7, a9, -4

	srli	a7, a3, 1
	rotw	1
	_bnei	a3, 1, .Lloop
	j	.Lexit

.Lc12:	_bbci.l	a3, 3, .Linvalid_mask	# bit 2 shouldn't be zero!

	/* 12-register frame (call12) */

	l32e	a2, a5, -12
	s32e	a8, a2, -48
	mov	a8, a2

.Lc12c: s32e	a9, a8, -44
	s32e	a10, a8, -40
	s32e	a11, a8, -36
	s32e	a12, a8, -32
	s32e	a13, a8, -28
	s32e	a14, a8, -24
	s32e	a15, a8, -20
	srli	a15, a3, 3

	/* The stack pointer for a4..a7 is out of reach, so we rotate the
	 * window, grab the stackpointer, and rotate back.
	 * Alternatively, we could also use the following approach, but that
	 * makes the fixup routine much more complicated:
	 * rotw	1
	 * s32e	a0, a13, -16
	 * ...
	 * rotw 2
	 */

	rotw	1
	mov	a5, a13
	rotw	-1

	s32e	a4, a9, -16
	s32e	a5, a9, -12
	s32e	a6, a9, -8
	s32e	a7, a9, -4

	rotw	3

	_beqi	a3, 1, .Lexit
	j	.Lloop

.Linvalid_mask:

	/* We get here because of an unrecoverable error in the window
	 * registers. If we are in user space, we kill the application,
	 * however, this condition is unrecoverable in kernel space.
	 */

	rsr	a0, PS
	_bbci.l	a0, PS_UM_SHIFT, 1f

 	/* User space: Setup a dummy frame and kill application.
	 * Note: We assume EXC_TABLE_KSTK contains a valid stack pointer.
	 */

	movi	a0, 1
	movi	a1, 0

	wsr	a0, WINDOWSTART
	wsr	a1, WINDOWBASE
	rsync

	movi	a0, 0

	movi	a3, exc_table
	l32i	a1, a3, EXC_TABLE_KSTK
	wsr	a3, EXCSAVE_1

	movi	a4, PS_WOE_MASK | 1
	wsr	a4, PS
	rsync

	movi	a6, SIGSEGV
	movi	a4, do_exit
	callx4	a4

1:	/* Kernel space: PANIC! */

	wsr	a0, EXCSAVE_1
	movi	a0, unrecoverable_exception
	callx0	a0		# should not return
1:	j	1b

/*
 * We should never get here. Bail out!
 */

ENTRY(fast_second_level_miss_double_kernel)

1:	movi	a0, unrecoverable_exception
	callx0	a0		# should not return
1:	j	1b

/* First-level entry handler for user, kernel, and double 2nd-level
 * TLB miss exceptions.  Note that for now, user and kernel miss
 * exceptions share the same entry point and are handled identically.
 *
 * An old, less-efficient C version of this function used to exist.
 * We include it below, interleaved as comments, for reference.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_second_level_miss)

	/* Save a1. Note: we don't expect a double exception. */

	s32i	a1, a2, PT_AREG1

	/* We need to map the page of PTEs for the user task.  Find
	 * the pointer to that page.  Also, it's possible for tsk->mm
	 * to be NULL while tsk->active_mm is nonzero if we faulted on
	 * a vmalloc address.  In that rare case, we must use
	 * active_mm instead to avoid a fault in this handler.  See
	 *
	 * http://mail.nl.linux.org/linux-mm/2002-08/msg00258.html
	 *   (or search Internet on "mm vs. active_mm")
	 *
	 *	if (!mm)
	 *		mm = tsk->active_mm;
	 *	pgd = pgd_offset (mm, regs->excvaddr);
	 *	pmd = pmd_offset (pgd, regs->excvaddr);
	 *	pmdval = *pmd;
	 */

	GET_CURRENT(a1,a2)
	l32i	a0, a1, TASK_MM		# tsk->mm
	beqz	a0, 9f

8:	rsr	a1, EXCVADDR		# fault address
	_PGD_OFFSET(a0, a1, a1)
	l32i	a0, a0, 0		# read pmdval
	//beqi	a0, _PAGE_USER, 2f
	beqz	a0, 2f

	/* Read ptevaddr and convert to top of page-table page.
	 *
	 * 	vpnval = read_ptevaddr_register() & PAGE_MASK;
	 * 	vpnval += DTLB_WAY_PGTABLE;
	 *	pteval = mk_pte (virt_to_page(pmd_val(pmdval)), PAGE_KERNEL);
	 *	write_dtlb_entry (pteval, vpnval);
	 *
	 * The messy computation for 'pteval' above really simplifies
	 * into the following:
	 *
	 * pteval = ((pmdval - PAGE_OFFSET) & PAGE_MASK) | PAGE_KERNEL
	 */

	movi	a1, -PAGE_OFFSET
	add	a0, a0, a1		# pmdval - PAGE_OFFSET
	extui	a1, a0, 0, PAGE_SHIFT	# ... & PAGE_MASK
	xor	a0, a0, a1


	movi	a1, PAGE_DIRECTORY
	or	a0, a0, a1		# ... | PAGE_DIRECTORY

	rsr	a1, PTEVADDR
	srli	a1, a1, PAGE_SHIFT
	slli	a1, a1, PAGE_SHIFT	# ptevaddr & PAGE_MASK
	addi	a1, a1, DTLB_WAY_PGTABLE	# ... + way_number

	wdtlb	a0, a1
	dsync

	/* Exit critical section. */

	movi	a0, 0
	s32i	a0, a3, EXC_TABLE_FIXUP

	/* Restore the working registers, and return. */

	l32i	a0, a2, PT_AREG0
	l32i	a1, a2, PT_AREG1
	l32i	a2, a2, PT_DEPC
	xsr	a3, EXCSAVE_1

	bgeui	a2, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	/* Restore excsave1 and return. */

	rsr	a2, DEPC
	rfe

	/* Return from double exception. */

1:	xsr	a2, DEPC
	esync
	rfde

9:	l32i	a0, a1, TASK_ACTIVE_MM	# unlikely case mm == 0
	j	8b

2:	/* Invalid PGD, default exception handling */

	rsr	a1, DEPC
	xsr	a3, EXCSAVE_1
	s32i	a1, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	mov	a1, a2

	rsr	a2, PS
	bbsi.l	a2, PS_UM_SHIFT, 1f
	j	_kernel_exception
1:	j	_user_exception


/*
 * StoreProhibitedException
 *
 * Update the pte and invalidate the itlb mapping for this pte.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_store_prohibited)

	/* Save a1 and a4. */

	s32i	a1, a2, PT_AREG1
	s32i	a4, a2, PT_AREG4

	GET_CURRENT(a1,a2)
	l32i	a0, a1, TASK_MM		# tsk->mm
	beqz	a0, 9f

8:	rsr	a1, EXCVADDR		# fault address
	_PGD_OFFSET(a0, a1, a4)
	l32i	a0, a0, 0
	//beqi	a0, _PAGE_USER, 2f	# FIXME use _PAGE_INVALID
	beqz	a0, 2f

	_PTE_OFFSET(a0, a1, a4)
	l32i	a4, a0, 0		# read pteval
	movi	a1, _PAGE_VALID | _PAGE_RW
	bnall	a4, a1, 2f

	movi	a1, _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_WRENABLE
	or	a4, a4, a1
	rsr	a1, EXCVADDR
	s32i	a4, a0, 0

	/* We need to flush the cache if we have page coloring. */
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	dhwb	a0, 0
#endif
	pdtlb	a0, a1
	beqz	a0, 1f
	idtlb	a0		// FIXME do we need this?
	wdtlb	a4, a0
1:

	/* Exit critical section. */

	movi	a0, 0
	s32i	a0, a3, EXC_TABLE_FIXUP

	/* Restore the working registers, and return. */

	l32i	a4, a2, PT_AREG4
	l32i	a1, a2, PT_AREG1
	l32i	a0, a2, PT_AREG0
	l32i	a2, a2, PT_DEPC

	/* Restore excsave1 and a3. */

	xsr	a3, EXCSAVE_1
	bgeui	a2, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	rsr	a2, DEPC
	rfe

	/* Double exception. Restore FIXUP handler and return. */

1:	xsr	a2, DEPC
	esync
	rfde

9:	l32i	a0, a1, TASK_ACTIVE_MM	# unlikely case mm == 0
	j	8b

2:	/* If there was a problem, handle fault in C */

	rsr	a4, DEPC	# still holds a2
	xsr	a3, EXCSAVE_1
	s32i	a4, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	l32i	a4, a2, PT_AREG4
	mov	a1, a2

	rsr	a2, PS
	bbsi.l	a2, PS_UM_SHIFT, 1f
	j	_kernel_exception
1:	j	_user_exception


#if XCHAL_EXTRA_SA_SIZE

#warning fast_coprocessor untested

/*
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_coprocessor_double)
	wsr	a0, EXCSAVE_1
	movi	a0, unrecoverable_exception
	callx0	a0

ENTRY(fast_coprocessor)

	/* Fatal if we are in a double exception. */

	l32i	a0, a2, PT_DEPC
	_bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, fast_coprocessor_double

	/* Save some registers a1, a3, a4, SAR */

	xsr	a3, EXCSAVE_1
	s32i	a3, a2, PT_AREG3
	rsr	a3, SAR
	s32i	a4, a2, PT_AREG4
	s32i	a1, a2, PT_AREG1
	s32i	a5, a1, PT_AREG5
	s32i	a3, a2, PT_SAR
	mov	a1, a2

	/* Currently, the HAL macros only guarantee saving a0 and a1.
	 * These can and will be refined in the future, but for now,
	 * just save the remaining registers of a2...a15.
	 */
	s32i	a6, a1, PT_AREG6
	s32i	a7, a1, PT_AREG7
	s32i	a8, a1, PT_AREG8
	s32i	a9, a1, PT_AREG9
	s32i	a10, a1, PT_AREG10
	s32i	a11, a1, PT_AREG11
	s32i	a12, a1, PT_AREG12
	s32i	a13, a1, PT_AREG13
	s32i	a14, a1, PT_AREG14
	s32i	a15, a1, PT_AREG15

	/* Find coprocessor number. Subtract first CP EXCCAUSE from EXCCAUSE */

	rsr	a0, EXCCAUSE
	addi	a3, a0, -XCHAL_EXCCAUSE_COPROCESSOR0_DISABLED

	/* Set corresponding CPENABLE bit */

	movi	a4, 1
	ssl	a3			# SAR: 32 - coprocessor_number
	rsr	a5, CPENABLE
	sll	a4, a4
	or	a4, a5, a4
	wsr	a4, CPENABLE
	rsync
	movi	a5, coprocessor_info	# list of owner and offset into cp_save
	addx8	a0, a4, a5		# entry for CP

	bne	a4, a5, .Lload		# bit wasn't set before, cp not in use

	/* Now compare the current task with the owner of the coprocessor.
	 * If they are the same, there is no reason to save or restore any
	 * coprocessor state. Having already enabled the coprocessor,
	 * branch ahead to return.
	 */
	GET_CURRENT(a5,a1)
	l32i	a4, a0, COPROCESSOR_INFO_OWNER	# a4: current owner for this CP
	beq	a4, a5, .Ldone

	/* Find location to dump current coprocessor state:
	 *  task_struct->task_cp_save_offset + coprocessor_offset[coprocessor]
	 *
	 * Note: a0 pointer to the entry in the coprocessor owner table,
	 *	 a3 coprocessor number,
         *	 a4 current owner of coprocessor.
	 */
	l32i	a5, a0, COPROCESSOR_INFO_OFFSET
	addi	a2, a4, THREAD_CP_SAVE
	add	a2, a2, a5

	/* Store current coprocessor states. (a5 still has CP number) */

	xchal_cpi_store_funcbody

	/* The macro might have destroyed a3 (coprocessor number), but
	 * SAR still has 32 - coprocessor_number!
	 */
	movi	a3, 32
	rsr	a4, SAR
	sub	a3, a3, a4

.Lload:	/* A new task now owns the corpocessors. Save its TCB pointer into
	 * the coprocessor owner table.
	 *
	 * Note: a0 pointer to the entry in the coprocessor owner table,
	 *	 a3 coprocessor number.
	 */
	GET_CURRENT(a4,a1)
	s32i	a4, a0, 0

	/* Find location from where to restore the current coprocessor state.*/

	l32i	a5, a0, COPROCESSOR_INFO_OFFSET
	addi	a2, a4, THREAD_CP_SAVE
	add	a2, a2, a4

	xchal_cpi_load_funcbody

	/* We must assume that the xchal_cpi_store_funcbody macro destroyed
	 * registers a2..a15.
	 */

.Ldone:	l32i	a15, a1, PT_AREG15
	l32i	a14, a1, PT_AREG14
	l32i	a13, a1, PT_AREG13
	l32i	a12, a1, PT_AREG12
	l32i	a11, a1, PT_AREG11
	l32i	a10, a1, PT_AREG10
	l32i	a9, a1, PT_AREG9
	l32i	a8, a1, PT_AREG8
	l32i	a7, a1, PT_AREG7
	l32i	a6, a1, PT_AREG6
	l32i	a5, a1, PT_AREG5
	l32i	a4, a1, PT_AREG4
	l32i	a3, a1, PT_AREG3
	l32i	a2, a1, PT_AREG2
	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1

	rfe

#endif /* XCHAL_EXTRA_SA_SIZE */

/*
 * Task switch.
 *
 * struct task*  _switch_to (struct task* prev, struct task* next)
 *         a2                              a2                 a3
 */

ENTRY(_switch_to)

	entry	a1, 16

	mov	a4, a3			# preserve a3

	s32i	a0, a2, THREAD_RA	# save return address
	s32i	a1, a2, THREAD_SP	# save stack pointer

	/* Disable ints while we manipulate the stack pointer; spill regs. */

	movi	a5, PS_EXCM_MASK | LOCKLEVEL
	xsr	a5, PS
	rsr	a3, EXCSAVE_1
	rsync
	s32i	a3, a3, EXC_TABLE_FIXUP	/* enter critical section */

	call0	_spill_registers

	/* Set kernel stack (and leave critical section)
	 * Note: It's save to set it here. The stack will not be overwritten
	 *       because the kernel stack will only be loaded again after
	 *       we return from kernel space.
	 */

	l32i	a0, a4, TASK_THREAD_INFO
	rsr	a3, EXCSAVE_1		# exc_table
	movi	a1, 0
	addi	a0, a0, PT_REGS_OFFSET
	s32i	a1, a3, EXC_TABLE_FIXUP
	s32i	a0, a3, EXC_TABLE_KSTK

	/* restore context of the task that 'next' addresses */

	l32i	a0, a4, THREAD_RA	/* restore return address */
	l32i	a1, a4, THREAD_SP	/* restore stack pointer */

	wsr	a5, PS
	rsync

	retw


ENTRY(ret_from_fork)

	/* void schedule_tail (struct task_struct *prev)
	 * Note: prev is still in a6 (return value from fake call4 frame)
	 */
	movi	a4, schedule_tail
	callx4	a4

	movi	a4, do_syscall_trace
	callx4	a4

	j	common_exception_return



/*
 * Table of syscalls
 */

.data
.align  4
.global sys_call_table
sys_call_table:

#define SYSCALL(call, narg) .word call
#include "syscalls.h"

/*
 * Number of arguments of each syscall
 */

.global sys_narg_table
sys_narg_table:

#undef SYSCALL
#define SYSCALL(call, narg) .byte narg
#include "syscalls.h"

