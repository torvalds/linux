/*
 * Low-level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 - 2008 by Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/asmmacro.h>
#include <asm/processor.h>
#include <asm/coprocessor.h>
#include <asm/thread_info.h>
#include <asm/asm-uaccess.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/tlbflush.h>
#include <variant/tie-asm.h>

/* Unimplemented features. */

#undef KERNEL_STACK_OVERFLOW_CHECK

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


	.macro	irq_save flags tmp
#if XTENSA_FAKE_NMI
#if defined(CONFIG_DEBUG_KERNEL) && (LOCKLEVEL | TOPLEVEL) >= XCHAL_DEBUGLEVEL
	rsr	\flags, ps
	extui	\tmp, \flags, PS_INTLEVEL_SHIFT, PS_INTLEVEL_WIDTH
	bgei	\tmp, LOCKLEVEL, 99f
	rsil	\tmp, LOCKLEVEL
99:
#else
	movi	\tmp, LOCKLEVEL
	rsr	\flags, ps
	or	\flags, \flags, \tmp
	xsr	\flags, ps
	rsync
#endif
#else
	rsil	\flags, LOCKLEVEL
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
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave1:	dispatch table
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
 * Note: _user_exception might be at an odd address. Don't use call0..call12
 */
	.literal_position

ENTRY(user_exception)

	/* Save a1, a2, a3, and set SP. */

	rsr	a0, depc
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	mov	a1, a2

	.globl _user_exception
_user_exception:

	/* Save SAR and turn off single stepping */

	movi	a2, 0
	wsr	a2, depc		# terminate user stack trace with 0
	rsr	a3, sar
	xsr	a2, icountlevel
	s32i	a3, a1, PT_SAR
	s32i	a2, a1, PT_ICOUNTLEVEL

#if XCHAL_HAVE_THREADPTR
	rur	a2, threadptr
	s32i	a2, a1, PT_THREADPTR
#endif

	/* Rotate ws so that the current windowbase is at bit0. */
	/* Assume ws = xxwww1yyyy. Rotate ws right, so that a2 = yyyyxxwww1 */

	rsr	a2, windowbase
	rsr	a3, windowstart
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
	 * All register frames starting from the top field to the marked '1'
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

	rsr	a2, sar			# original WINDOWBASE
	movi	a3, 1
	ssl	a2
	sll	a3, a3
	wsr	a3, windowstart		# set corresponding WINDOWSTART bit
	wsr	a2, windowbase		# and WINDOWSTART
	rsync

	/* We are back to the original stack pointer (a1) */

2:	/* Now, jump to the common exception handler. */

	j	common_exception

ENDPROC(user_exception)

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
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
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
 * Note: _kernel_exception might be at an odd address. Don't use call0..call12
 */

ENTRY(kernel_exception)

	/* Save a1, a2, a3, and set SP. */

	rsr	a0, depc		# get a2
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_AREG2
	s32i	a3, a2, PT_AREG3
	mov	a1, a2

	.globl _kernel_exception
_kernel_exception:

	/* Save SAR and turn off single stepping */

	movi	a2, 0
	rsr	a3, sar
	xsr	a2, icountlevel
	s32i	a3, a1, PT_SAR
	s32i	a2, a1, PT_ICOUNTLEVEL

	/* Rotate ws so that the current windowbase is at bit0. */
	/* Assume ws = xxwww1yyyy. Rotate ws right, so that a2 = yyyyxxwww1 */

	rsr	a2, windowbase		# don't need to save these, we only
	rsr	a3, windowstart		# need shifted windowstart: windowmask
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

	_bnei	a2, 1, 1f

	/* Copy spill slots of a0 and a1 to imitate movsp
	 * in order to keep exception stack continuous
	 */
	l32i	a3, a1, PT_SIZE
	l32i	a0, a1, PT_SIZE + 4
	s32e	a3, a1, -16
	s32e	a0, a1, -12
1:
	l32i	a0, a1, PT_AREG0	# restore saved a0
	wsr	a0, depc

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

	/* Save some registers, disable loops and clear the syscall flag. */

	rsr	a2, debugcause
	rsr	a3, epc1
	s32i	a2, a1, PT_DEBUGCAUSE
	s32i	a3, a1, PT_PC

	movi	a2, NO_SYSCALL
	rsr	a3, excvaddr
	s32i	a2, a1, PT_SYSCALL
	movi	a2, 0
	s32i	a3, a1, PT_EXCVADDR
#if XCHAL_HAVE_LOOPS
	xsr	a2, lcount
	s32i	a2, a1, PT_LCOUNT
#endif

	/* It is now save to restore the EXC_TABLE_FIXUP variable. */

	rsr	a2, exccause
	movi	a3, 0
	rsr	a0, excsave1
	s32i	a2, a1, PT_EXCCAUSE
	s32i	a3, a0, EXC_TABLE_FIXUP

	/* All unrecoverable states are saved on stack, now, and a1 is valid.
	 * Now we can allow exceptions again. In case we've got an interrupt
	 * PS.INTLEVEL is set to LOCKLEVEL disabling furhter interrupts,
	 * otherwise it's left unchanged.
	 *
	 * Set PS(EXCM = 0, UM = 0, RING = 0, OWB = 0, WOE = 1, INTLEVEL = X)
	 */

	rsr	a3, ps
	s32i	a3, a1, PT_PS		# save ps

#if XTENSA_FAKE_NMI
	/* Correct PS needs to be saved in the PT_PS:
	 * - in case of exception or level-1 interrupt it's in the PS,
	 *   and is already saved.
	 * - in case of medium level interrupt it's in the excsave2.
	 */
	movi	a0, EXCCAUSE_MAPPED_NMI
	extui	a3, a3, PS_INTLEVEL_SHIFT, PS_INTLEVEL_WIDTH
	beq	a2, a0, .Lmedium_level_irq
	bnei	a2, EXCCAUSE_LEVEL1_INTERRUPT, .Lexception
	beqz	a3, .Llevel1_irq	# level-1 IRQ sets ps.intlevel to 0

.Lmedium_level_irq:
	rsr	a0, excsave2
	s32i	a0, a1, PT_PS		# save medium-level interrupt ps
	bgei	a3, LOCKLEVEL, .Lexception

.Llevel1_irq:
	movi	a3, LOCKLEVEL

.Lexception:
	movi	a0, PS_WOE_MASK
	or	a3, a3, a0
#else
	addi	a2, a2, -EXCCAUSE_LEVEL1_INTERRUPT
	movi	a0, LOCKLEVEL
	extui	a3, a3, PS_INTLEVEL_SHIFT, PS_INTLEVEL_WIDTH
					# a3 = PS.INTLEVEL
	moveqz	a3, a0, a2		# a3 = LOCKLEVEL iff interrupt
	movi	a2, PS_WOE_MASK
	or	a3, a3, a2
	rsr	a2, exccause
#endif

	/* restore return address (or 0 if return to userspace) */
	rsr	a0, depc
	wsr	a3, ps
	rsync				# PS.WOE => rsync => overflow

	/* Save lbeg, lend */
#if XCHAL_HAVE_LOOPS
	rsr	a4, lbeg
	rsr	a3, lend
	s32i	a4, a1, PT_LBEG
	s32i	a3, a1, PT_LEND
#endif

	/* Save SCOMPARE1 */

#if XCHAL_HAVE_S32C1I
	rsr     a3, scompare1
	s32i    a3, a1, PT_SCOMPARE1
#endif

	/* Save optional registers. */

	save_xtregs_opt a1 a3 a4 a5 a6 a7 PT_XTREGS_OPT
	
	/* Go to second-level dispatcher. Set up parameters to pass to the
	 * exception handler and call the exception handler.
	 */

	rsr	a4, excsave1
	mov	a6, a1			# pass stack frame
	mov	a7, a2			# pass EXCCAUSE
	addx4	a4, a2, a4
	l32i	a4, a4, EXC_TABLE_DEFAULT		# load handler

	/* Call the second-level handler */

	callx4	a4

	/* Jump here for exception exit */
	.global common_exception_return
common_exception_return:

#if XTENSA_FAKE_NMI
	l32i	a2, a1, PT_EXCCAUSE
	movi	a3, EXCCAUSE_MAPPED_NMI
	beq	a2, a3, .LNMIexit
#endif
1:
	irq_save a2, a3
#ifdef CONFIG_TRACE_IRQFLAGS
	call4	trace_hardirqs_off
#endif

	/* Jump if we are returning from kernel exceptions. */

	l32i	a3, a1, PT_PS
	GET_THREAD_INFO(a2, a1)
	l32i	a4, a2, TI_FLAGS
	_bbci.l	a3, PS_UM_BIT, 6f

	/* Specific to a user exception exit:
	 * We need to check some flags for signal handling and rescheduling,
	 * and have to restore WB and WS, extra states, and all registers
	 * in the register file that were in use in the user task.
	 * Note that we don't disable interrupts here. 
	 */

	_bbsi.l	a4, TIF_NEED_RESCHED, 3f
	_bbsi.l	a4, TIF_NOTIFY_RESUME, 2f
	_bbci.l	a4, TIF_SIGPENDING, 5f

2:	l32i	a4, a1, PT_DEPC
	bgeui	a4, VALID_DOUBLE_EXCEPTION_ADDRESS, 4f

	/* Call do_signal() */

#ifdef CONFIG_TRACE_IRQFLAGS
	call4	trace_hardirqs_on
#endif
	rsil	a2, 0
	mov	a6, a1
	call4	do_notify_resume	# int do_notify_resume(struct pt_regs*)
	j	1b

3:	/* Reschedule */

#ifdef CONFIG_TRACE_IRQFLAGS
	call4	trace_hardirqs_on
#endif
	rsil	a2, 0
	call4	schedule	# void schedule (void)
	j	1b

#ifdef CONFIG_PREEMPT
6:
	_bbci.l	a4, TIF_NEED_RESCHED, 4f

	/* Check current_thread_info->preempt_count */

	l32i	a4, a2, TI_PRE_COUNT
	bnez	a4, 4f
	call4	preempt_schedule_irq
	j	4f
#endif

#if XTENSA_FAKE_NMI
.LNMIexit:
	l32i	a3, a1, PT_PS
	_bbci.l	a3, PS_UM_BIT, 4f
#endif

5:
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	_bbci.l	a4, TIF_DB_DISABLED, 7f
	call4	restore_dbreak
7:
#endif
#ifdef CONFIG_DEBUG_TLB_SANITY
	l32i	a4, a1, PT_DEPC
	bgeui	a4, VALID_DOUBLE_EXCEPTION_ADDRESS, 4f
	call4	check_tlb_sanity
#endif
6:
4:
#ifdef CONFIG_TRACE_IRQFLAGS
	extui	a4, a3, PS_INTLEVEL_SHIFT, PS_INTLEVEL_WIDTH
	bgei	a4, LOCKLEVEL, 1f
	call4	trace_hardirqs_on
1:
#endif
	/* Restore optional registers. */

	load_xtregs_opt a1 a2 a4 a5 a6 a7 PT_XTREGS_OPT

	/* Restore SCOMPARE1 */

#if XCHAL_HAVE_S32C1I
	l32i    a2, a1, PT_SCOMPARE1
	wsr     a2, scompare1
#endif
	wsr	a3, ps		/* disable interrupts */

	_bbci.l	a3, PS_UM_BIT, kernel_exception_exit

user_exception_exit:

	/* Restore the state of the task and return from the exception. */

	/* Switch to the user thread WINDOWBASE. Save SP temporarily in DEPC */

	l32i	a2, a1, PT_WINDOWBASE
	l32i	a3, a1, PT_WINDOWSTART
	wsr	a1, depc		# use DEPC as temp storage
	wsr	a3, windowstart		# restore WINDOWSTART
	ssr	a2			# preserve user's WB in the SAR
	wsr	a2, windowbase		# switch to user's saved WB
	rsync
	rsr	a1, depc		# restore stack pointer
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

1:	rsr	a0, windowbase
	rsr	a3, sar
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

2:
#if XCHAL_HAVE_THREADPTR
	l32i	a3, a1, PT_THREADPTR
	wur	a3, threadptr
#endif

	j	common_exception_exit

	/* This is the kernel exception exit.
	 * We avoided to do a MOVSP when we entered the exception, but we
	 * have to do it here.
	 */

kernel_exception_exit:

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

	rsr     a3, windowstart
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

	/* Restore address registers. */

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
	wsr	a2, epc1
	wsr	a3, sar

	/* Restore LBEG, LEND, LCOUNT */
#if XCHAL_HAVE_LOOPS
	l32i	a2, a1, PT_LBEG
	l32i	a3, a1, PT_LEND
	wsr	a2, lbeg
	l32i	a2, a1, PT_LCOUNT
	wsr	a3, lend
	wsr	a2, lcount
#endif

	/* We control single stepping through the ICOUNTLEVEL register. */

	l32i	a2, a1, PT_ICOUNTLEVEL
	movi	a3, -2
	wsr	a2, icountlevel
	wsr	a3, icount

	/* Check if it was double exception. */

	l32i	a0, a1, PT_DEPC
	l32i	a3, a1, PT_AREG3
	l32i	a2, a1, PT_AREG2
	_bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	/* Restore a0...a3 and return */

	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1
	rfe

1: 	wsr	a0, depc
	l32i	a0, a1, PT_AREG0
	l32i	a1, a1, PT_AREG1
	rfde

ENDPROC(kernel_exception)

/*
 * Debug exception handler.
 *
 * Currently, we don't support KGDB, so only user application can be debugged.
 *
 * When we get here,  a0 is trashed and saved to excsave[debuglevel]
 */

	.literal_position

ENTRY(debug_exception)

	rsr	a0, SREG_EPS + XCHAL_DEBUGLEVEL
	bbsi.l	a0, PS_EXCM_BIT, 1f	# exception mode

	/* Set EPC1 and EXCCAUSE */

	wsr	a2, depc		# save a2 temporarily
	rsr	a2, SREG_EPC + XCHAL_DEBUGLEVEL
	wsr	a2, epc1

	movi	a2, EXCCAUSE_MAPPED_DEBUG
	wsr	a2, exccause

	/* Restore PS to the value before the debug exc but with PS.EXCM set.*/

	movi	a2, 1 << PS_EXCM_BIT
	or	a2, a0, a2
	wsr	a2, ps

	/* Switch to kernel/user stack, restore jump vector, and save a0 */

	bbsi.l	a2, PS_UM_BIT, 2f	# jump if user mode

	addi	a2, a1, -16-PT_SIZE	# assume kernel stack
3:
	l32i	a0, a3, DT_DEBUG_SAVE
	s32i	a1, a2, PT_AREG1
	s32i	a0, a2, PT_AREG0
	movi	a0, 0
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	xsr	a3, SREG_EXCSAVE + XCHAL_DEBUGLEVEL
	xsr	a0, depc
	s32i	a3, a2, PT_AREG3
	s32i	a0, a2, PT_AREG2
	mov	a1, a2

	/* Debug exception is handled as an exception, so interrupts will
	 * likely be enabled in the common exception handler. Disable
	 * preemption if we have HW breakpoints to preserve DEBUGCAUSE.DBNUM
	 * meaning.
	 */
#if defined(CONFIG_PREEMPT_COUNT) && defined(CONFIG_HAVE_HW_BREAKPOINT)
	GET_THREAD_INFO(a2, a1)
	l32i	a3, a2, TI_PRE_COUNT
	addi	a3, a3, 1
	s32i	a3, a2, TI_PRE_COUNT
#endif

	rsr	a2, ps
	bbsi.l	a2, PS_UM_BIT, _user_exception
	j	_kernel_exception

2:	rsr	a2, excsave1
	l32i	a2, a2, EXC_TABLE_KSTK	# load kernel stack pointer
	j	3b

#ifdef CONFIG_HAVE_HW_BREAKPOINT
	/* Debug exception while in exception mode. This may happen when
	 * window overflow/underflow handler or fast exception handler hits
	 * data breakpoint, in which case save and disable all data
	 * breakpoints, single-step faulting instruction and restore data
	 * breakpoints.
	 */
1:
	bbci.l	a0, PS_UM_BIT, 1b	# jump if kernel mode

	rsr	a0, debugcause
	bbsi.l	a0, DEBUGCAUSE_DBREAK_BIT, .Ldebug_save_dbreak

	.set	_index, 0
	.rept	XCHAL_NUM_DBREAK
	l32i	a0, a3, DT_DBREAKC_SAVE + _index * 4
	wsr	a0, SREG_DBREAKC + _index
	.set	_index, _index + 1
	.endr

	l32i	a0, a3, DT_ICOUNT_LEVEL_SAVE
	wsr	a0, icountlevel

	l32i	a0, a3, DT_ICOUNT_SAVE
	xsr	a0, icount

	l32i	a0, a3, DT_DEBUG_SAVE
	xsr	a3, SREG_EXCSAVE + XCHAL_DEBUGLEVEL
	rfi	XCHAL_DEBUGLEVEL

.Ldebug_save_dbreak:
	.set	_index, 0
	.rept	XCHAL_NUM_DBREAK
	movi	a0, 0
	xsr	a0, SREG_DBREAKC + _index
	s32i	a0, a3, DT_DBREAKC_SAVE + _index * 4
	.set	_index, _index + 1
	.endr

	movi	a0, XCHAL_EXCM_LEVEL + 1
	xsr	a0, icountlevel
	s32i	a0, a3, DT_ICOUNT_LEVEL_SAVE

	movi	a0, 0xfffffffe
	xsr	a0, icount
	s32i	a0, a3, DT_ICOUNT_SAVE

	l32i	a0, a3, DT_DEBUG_SAVE
	xsr	a3, SREG_EXCSAVE + XCHAL_DEBUGLEVEL
	rfi	XCHAL_DEBUGLEVEL
#else
	/* Debug exception while in exception mode. Should not happen. */
1:	j	1b	// FIXME!!
#endif

ENDPROC(debug_exception)

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

	.literal_position

ENTRY(unrecoverable_exception)

	movi	a0, 1
	movi	a1, 0

	wsr	a0, windowstart
	wsr	a1, windowbase
	rsync

	movi	a1, PS_WOE_MASK | LOCKLEVEL
	wsr	a1, ps
	rsync

	movi	a1, init_task
	movi	a0, 0
	addi	a1, a1, PT_REGS_OFFSET

	movi	a6, unrecoverable_text
	call4	panic

1:	j	1b

ENDPROC(unrecoverable_exception)

/* -------------------------- FAST EXCEPTION HANDLERS ----------------------- */

/*
 * Fast-handler for alloca exceptions
 *
 *  The ALLOCA handler is entered when user code executes the MOVSP
 *  instruction and the caller's frame is not in the register file.
 *
 * This algorithm was taken from the Ross Morley's RTOS Porting Layer:
 *
 *    /home/ross/rtos/porting/XtensaRTOS-PortingLayer-20090507/xtensa_vectors.S
 *
 * It leverages the existing window spill/fill routines and their support for
 * double exceptions. The 'movsp' instruction will only cause an exception if
 * the next window needs to be loaded. In fact this ALLOCA exception may be
 * replaced at some point by changing the hardware to do a underflow exception
 * of the proper size instead.
 *
 * This algorithm simply backs out the register changes started by the user
 * excpetion handler, makes it appear that we have started a window underflow
 * by rotating the window back and then setting the old window base (OWB) in
 * the 'ps' register with the rolled back window base. The 'movsp' instruction
 * will be re-executed and this time since the next window frames is in the
 * active AR registers it won't cause an exception.
 *
 * If the WindowUnderflow code gets a TLB miss the page will get mapped
 * the the partial windeowUnderflow will be handeled in the double exception
 * handler.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_alloca)
	rsr	a0, windowbase
	rotw	-1
	rsr	a2, ps
	extui	a3, a2, PS_OWB_SHIFT, PS_OWB_WIDTH
	xor	a3, a3, a4
	l32i	a4, a6, PT_AREG0
	l32i	a1, a6, PT_DEPC
	rsr	a6, depc
	wsr	a1, depc
	slli	a3, a3, PS_OWB_SHIFT
	xor	a2, a2, a3
	wsr	a2, ps
	rsync

	_bbci.l	a4, 31, 4f
	rotw	-1
	_bbci.l	a8, 30, 8f
	rotw	-1
	j	_WindowUnderflow12
8:	j	_WindowUnderflow8
4:	j	_WindowUnderflow4
ENDPROC(fast_alloca)

#ifdef CONFIG_USER_ABI_CALL0_PROBE
/*
 * fast illegal instruction handler.
 *
 * This is used to fix up user PS.WOE on the exception caused
 * by the first opcode related to register window. If PS.WOE is
 * already set it goes directly to the common user exception handler.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 */

ENTRY(fast_illegal_instruction_user)

	rsr	a0, ps
	bbsi.l	a0, PS_WOE_BIT, user_exception
	s32i	a3, a2, PT_AREG3
	movi	a3, PS_WOE_MASK
	or	a0, a0, a3
	wsr	a0, ps
	l32i	a3, a2, PT_AREG3
	l32i	a0, a2, PT_AREG0
	rsr	a2, depc
	rfe

ENDPROC(fast_illegal_instruction_user)
#endif

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
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 */

ENTRY(fast_syscall_user)

	/* Skip syscall. */

	rsr	a0, epc1
	addi	a0, a0, 3
	wsr	a0, epc1

	l32i	a0, a2, PT_DEPC
	bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, fast_syscall_unrecoverable

	rsr	a0, depc			# get syscall-nr
	_beqz	a0, fast_syscall_spill_registers
	_beqi	a0, __NR_xtensa, fast_syscall_xtensa

	j	user_exception

ENDPROC(fast_syscall_user)

ENTRY(fast_syscall_unrecoverable)

	/* Restore all states. */

	l32i    a0, a2, PT_AREG0        # restore a0
	xsr     a2, depc                # restore a2, depc

	wsr     a0, excsave1
	call0	unrecoverable_exception

ENDPROC(fast_syscall_unrecoverable)

/*
 * sysxtensa syscall handler
 *
 * int sysxtensa (SYS_XTENSA_ATOMIC_SET,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_ADD,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_EXG_ADD, ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_CMP_SWP, ptr, oldval, newval);
 *        a2            a6                   a3    a4      a5
 *
 * Entry condition:
 *
 *   a0:	a2 (syscall-nr), original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in a0 and DEPC
 *   a3:	a3
 *   a4..a15:	unchanged
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Note: we don't have to save a2; a2 holds the return value
 */

	.literal_position

#ifdef CONFIG_FAST_SYSCALL_XTENSA

ENTRY(fast_syscall_xtensa)

	s32i	a7, a2, PT_AREG7	# we need an additional register
	movi	a7, 4			# sizeof(unsigned int)
	access_ok a3, a7, a0, a2, .Leac	# a0: scratch reg, a2: sp

	_bgeui	a6, SYS_XTENSA_COUNT, .Lill
	_bnei	a6, SYS_XTENSA_ATOMIC_CMP_SWP, .Lnswp

	/* Fall through for ATOMIC_CMP_SWP. */

.Lswp:	/* Atomic compare and swap */

EX(.Leac) l32i	a0, a3, 0		# read old value
	bne	a0, a4, 1f		# same as old value? jump
EX(.Leac) s32i	a5, a3, 0		# different, modify value
	l32i	a7, a2, PT_AREG7	# restore a7
	l32i	a0, a2, PT_AREG0	# restore a0
	movi	a2, 1			# and return 1
	rfe

1:	l32i	a7, a2, PT_AREG7	# restore a7
	l32i	a0, a2, PT_AREG0	# restore a0
	movi	a2, 0			# return 0 (note that we cannot set
	rfe

.Lnswp:	/* Atomic set, add, and exg_add. */

EX(.Leac) l32i	a7, a3, 0		# orig
	addi	a6, a6, -SYS_XTENSA_ATOMIC_SET
	add	a0, a4, a7		# + arg
	moveqz	a0, a4, a6		# set
	addi	a6, a6, SYS_XTENSA_ATOMIC_SET
EX(.Leac) s32i	a0, a3, 0		# write new value

	mov	a0, a2
	mov	a2, a7
	l32i	a7, a0, PT_AREG7	# restore a7
	l32i	a0, a0, PT_AREG0	# restore a0
	rfe

.Leac:	l32i	a7, a2, PT_AREG7	# restore a7
	l32i	a0, a2, PT_AREG0	# restore a0
	movi	a2, -EFAULT
	rfe

.Lill:	l32i	a7, a2, PT_AREG7	# restore a7
	l32i	a0, a2, PT_AREG0	# restore a0
	movi	a2, -EINVAL
	rfe

ENDPROC(fast_syscall_xtensa)

#else /* CONFIG_FAST_SYSCALL_XTENSA */

ENTRY(fast_syscall_xtensa)

	l32i    a0, a2, PT_AREG0        # restore a0
	movi	a2, -ENOSYS
	rfe

ENDPROC(fast_syscall_xtensa)

#endif /* CONFIG_FAST_SYSCALL_XTENSA */


/* fast_syscall_spill_registers.
 *
 * Entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original in DEPC
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 * Note: We assume the stack pointer is EXC_TABLE_KSTK in the fixup handler.
 */

#ifdef CONFIG_FAST_SYSCALL_SPILL_REGISTERS

ENTRY(fast_syscall_spill_registers)

	/* Register a FIXUP handler (pass current wb as a parameter) */

	xsr	a3, excsave1
	movi	a0, fast_syscall_spill_registers_fixup
	s32i	a0, a3, EXC_TABLE_FIXUP
	rsr	a0, windowbase
	s32i	a0, a3, EXC_TABLE_PARAM
	xsr	a3, excsave1		# restore a3 and excsave_1

	/* Save a3, a4 and SAR on stack. */

	rsr	a0, sar
	s32i	a3, a2, PT_AREG3
	s32i	a0, a2, PT_SAR

	/* The spill routine might clobber a4, a7, a8, a11, a12, and a15. */

	s32i	a4, a2, PT_AREG4
	s32i	a7, a2, PT_AREG7
	s32i	a8, a2, PT_AREG8
	s32i	a11, a2, PT_AREG11
	s32i	a12, a2, PT_AREG12
	s32i	a15, a2, PT_AREG15

	/*
	 * Rotate ws so that the current windowbase is at bit 0.
	 * Assume ws = xxxwww1yy (www1 current window frame).
	 * Rotate ws right so that a4 = yyxxxwww1.
	 */

	rsr	a0, windowbase
	rsr	a3, windowstart		# a3 = xxxwww1yy
	ssr	a0			# holds WB
	slli	a0, a3, WSBITS
	or	a3, a3, a0		# a3 = xxxwww1yyxxxwww1yy
	srl	a3, a3			# a3 = 00xxxwww1yyxxxwww1

	/* We are done if there are no more than the current register frame. */

	extui	a3, a3, 1, WSBITS-1	# a3 = 0yyxxxwww
	movi	a0, (1 << (WSBITS-1))
	_beqz	a3, .Lnospill		# only one active frame? jump

	/* We want 1 at the top, so that we return to the current windowbase */

	or	a3, a3, a0		# 1yyxxxwww

	/* Skip empty frames - get 'oldest' WINDOWSTART-bit. */

	wsr	a3, windowstart		# save shifted windowstart
	neg	a0, a3
	and	a3, a0, a3		# first bit set from right: 000010000

	ffs_ws	a0, a3			# a0: shifts to skip empty frames
	movi	a3, WSBITS
	sub	a0, a3, a0		# WSBITS-a0:number of 0-bits from right
	ssr	a0			# save in SAR for later.

	rsr	a3, windowbase
	add	a3, a3, a0
	wsr	a3, windowbase
	rsync

	rsr	a3, windowstart
	srl	a3, a3			# shift windowstart

	/* WB is now just one frame below the oldest frame in the register
	   window. WS is shifted so the oldest frame is in bit 0, thus, WB
	   and WS differ by one 4-register frame. */

	/* Save frames. Depending what call was used (call4, call8, call12),
	 * we have to save 4,8. or 12 registers.
	 */


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
	j	.Lexit

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

	l32e	a0, a5, -12
	s32e	a8, a0, -48
	mov	a8, a0

	s32e	a9, a8, -44
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
	mov	a4, a13
	rotw	-1

	s32e	a4, a8, -16
	s32e	a5, a8, -12
	s32e	a6, a8, -8
	s32e	a7, a8, -4

	rotw	3

	_beqi	a3, 1, .Lexit
	j	.Lloop

.Lexit:

	/* Done. Do the final rotation and set WS */

	rotw	1
	rsr	a3, windowbase
	ssl	a3
	movi	a3, 1
	sll	a3, a3
	wsr	a3, windowstart
.Lnospill:

	/* Advance PC, restore registers and SAR, and return from exception. */

	l32i	a3, a2, PT_SAR
	l32i	a0, a2, PT_AREG0
	wsr	a3, sar
	l32i	a3, a2, PT_AREG3

	/* Restore clobbered registers. */

	l32i	a4, a2, PT_AREG4
	l32i	a7, a2, PT_AREG7
	l32i	a8, a2, PT_AREG8
	l32i	a11, a2, PT_AREG11
	l32i	a12, a2, PT_AREG12
	l32i	a15, a2, PT_AREG15

	movi	a2, 0
	rfe

.Linvalid_mask:

	/* We get here because of an unrecoverable error in the window
	 * registers, so set up a dummy frame and kill the user application.
	 * Note: We assume EXC_TABLE_KSTK contains a valid stack pointer.
	 */

	movi	a0, 1
	movi	a1, 0

	wsr	a0, windowstart
	wsr	a1, windowbase
	rsync

	movi	a0, 0

	rsr	a3, excsave1
	l32i	a1, a3, EXC_TABLE_KSTK

	movi	a4, PS_WOE_MASK | LOCKLEVEL
	wsr	a4, ps
	rsync

	movi	a6, SIGSEGV
	call4	do_exit

	/* shouldn't return, so panic */

	wsr	a0, excsave1
	call0	unrecoverable_exception		# should not return
1:	j	1b


ENDPROC(fast_syscall_spill_registers)

/* Fixup handler.
 *
 * We get here if the spill routine causes an exception, e.g. tlb miss.
 * We basically restore WINDOWBASE and WINDOWSTART to the condition when
 * we entered the spill routine and jump to the user exception handler.
 *
 * Note that we only need to restore the bits in windowstart that have not
 * been spilled yet by the _spill_register routine. Luckily, a3 contains a
 * rotated windowstart with only those bits set for frames that haven't been
 * spilled yet. Because a3 is rotated such that bit 0 represents the register
 * frame for the current windowbase - 1, we need to rotate a3 left by the
 * value of the current windowbase + 1 and move it to windowstart.
 *
 * a0: value of depc, original value in depc
 * a2: trashed, original value in EXC_TABLE_DOUBLE_SAVE
 * a3: exctable, original value in excsave1
 */

ENTRY(fast_syscall_spill_registers_fixup)

	rsr	a2, windowbase	# get current windowbase (a2 is saved)
	xsr	a0, depc	# restore depc and a0
	ssl	a2		# set shift (32 - WB)

	/* We need to make sure the current registers (a0-a3) are preserved.
	 * To do this, we simply set the bit for the current window frame
	 * in WS, so that the exception handlers save them to the task stack.
	 *
	 * Note: we use a3 to set the windowbase, so we take a special care
	 * of it, saving it in the original _spill_registers frame across
	 * the exception handler call.
	 */

	xsr	a3, excsave1	# get spill-mask
	slli	a3, a3, 1	# shift left by one
	addi	a3, a3, 1	# set the bit for the current window frame

	slli	a2, a3, 32-WSBITS
	src	a2, a3, a2	# a2 = xxwww1yyxxxwww1yy......
	wsr	a2, windowstart	# set corrected windowstart

	srli	a3, a3, 1
	rsr	a2, excsave1
	l32i	a2, a2, EXC_TABLE_DOUBLE_SAVE	# restore a2
	xsr	a2, excsave1
	s32i	a3, a2, EXC_TABLE_DOUBLE_SAVE	# save a3
	l32i	a3, a2, EXC_TABLE_PARAM	# original WB (in user task)
	xsr	a2, excsave1

	/* Return to the original (user task) WINDOWBASE.
	 * We leave the following frame behind:
	 * a0, a1, a2	same
	 * a3:		trashed (saved in EXC_TABLE_DOUBLE_SAVE)
	 * depc:	depc (we have to return to that address)
	 * excsave_1:	exctable
	 */

	wsr	a3, windowbase
	rsync

	/* We are now in the original frame when we entered _spill_registers:
	 *  a0: return address
	 *  a1: used, stack pointer
	 *  a2: kernel stack pointer
	 *  a3: available
	 *  depc: exception address
	 *  excsave: exctable
	 * Note: This frame might be the same as above.
	 */

	/* Setup stack pointer. */

	addi	a2, a2, -PT_USER_SIZE
	s32i	a0, a2, PT_AREG0

	/* Make sure we return to this fixup handler. */

	movi	a3, fast_syscall_spill_registers_fixup_return
	s32i	a3, a2, PT_DEPC		# setup depc

	/* Jump to the exception handler. */

	rsr	a3, excsave1
	rsr	a0, exccause
	addx4	a0, a0, a3              	# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER     # load handler
	l32i	a3, a3, EXC_TABLE_DOUBLE_SAVE
	jx	a0

ENDPROC(fast_syscall_spill_registers_fixup)

ENTRY(fast_syscall_spill_registers_fixup_return)

	/* When we return here, all registers have been restored (a2: DEPC) */

	wsr	a2, depc		# exception address

	/* Restore fixup handler. */

	rsr	a2, excsave1
	s32i	a3, a2, EXC_TABLE_DOUBLE_SAVE
	movi	a3, fast_syscall_spill_registers_fixup
	s32i	a3, a2, EXC_TABLE_FIXUP
	rsr	a3, windowbase
	s32i	a3, a2, EXC_TABLE_PARAM
	l32i	a2, a2, EXC_TABLE_KSTK

	/* Load WB at the time the exception occurred. */

	rsr	a3, sar			# WB is still in SAR
	neg	a3, a3
	wsr	a3, windowbase
	rsync

	rsr	a3, excsave1
	l32i	a3, a3, EXC_TABLE_DOUBLE_SAVE

	rfde

ENDPROC(fast_syscall_spill_registers_fixup_return)

#else /* CONFIG_FAST_SYSCALL_SPILL_REGISTERS */

ENTRY(fast_syscall_spill_registers)

	l32i    a0, a2, PT_AREG0        # restore a0
	movi	a2, -ENOSYS
	rfe

ENDPROC(fast_syscall_spill_registers)

#endif /* CONFIG_FAST_SYSCALL_SPILL_REGISTERS */

#ifdef CONFIG_MMU
/*
 * We should never get here. Bail out!
 */

ENTRY(fast_second_level_miss_double_kernel)

1:
	call0	unrecoverable_exception		# should not return
1:	j	1b

ENDPROC(fast_second_level_miss_double_kernel)

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
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_second_level_miss)

	/* Save a1 and a3. Note: we don't expect a double exception. */

	s32i	a1, a2, PT_AREG1
	s32i	a3, a2, PT_AREG3

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

8:	rsr	a3, excvaddr		# fault address
	_PGD_OFFSET(a0, a3, a1)
	l32i	a0, a0, 0		# read pmdval
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
	 * pteval = ((pmdval - PAGE_OFFSET + PHYS_OFFSET) & PAGE_MASK)
	 *                 | PAGE_DIRECTORY
	 */

	movi	a1, (PHYS_OFFSET - PAGE_OFFSET) & 0xffffffff
	add	a0, a0, a1		# pmdval - PAGE_OFFSET
	extui	a1, a0, 0, PAGE_SHIFT	# ... & PAGE_MASK
	xor	a0, a0, a1

	movi	a1, _PAGE_DIRECTORY
	or	a0, a0, a1		# ... | PAGE_DIRECTORY

	/*
	 * We utilize all three wired-ways (7-9) to hold pmd translations.
	 * Memory regions are mapped to the DTLBs according to bits 28 and 29.
	 * This allows to map the three most common regions to three different
	 * DTLBs:
	 *  0,1 -> way 7	program (0040.0000) and virtual (c000.0000)
	 *  2   -> way 8	shared libaries (2000.0000)
	 *  3   -> way 0	stack (3000.0000)
	 */

	extui	a3, a3, 28, 2		# addr. bit 28 and 29	0,1,2,3
	rsr	a1, ptevaddr
	addx2	a3, a3, a3		# ->			0,3,6,9
	srli	a1, a1, PAGE_SHIFT
	extui	a3, a3, 2, 2		# ->			0,0,1,2
	slli	a1, a1, PAGE_SHIFT	# ptevaddr & PAGE_MASK
	addi	a3, a3, DTLB_WAY_PGD
	add	a1, a1, a3		# ... + way_number

3:	wdtlb	a0, a1
	dsync

	/* Exit critical section. */

4:	rsr	a3, excsave1
	movi	a0, 0
	s32i	a0, a3, EXC_TABLE_FIXUP

	/* Restore the working registers, and return. */

	l32i	a0, a2, PT_AREG0
	l32i	a1, a2, PT_AREG1
	l32i	a3, a2, PT_AREG3
	l32i	a2, a2, PT_DEPC

	bgeui	a2, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	/* Restore excsave1 and return. */

	rsr	a2, depc
	rfe

	/* Return from double exception. */

1:	xsr	a2, depc
	esync
	rfde

9:	l32i	a0, a1, TASK_ACTIVE_MM	# unlikely case mm == 0
	bnez	a0, 8b

	/* Even more unlikely case active_mm == 0.
	 * We can get here with NMI in the middle of context_switch that
	 * touches vmalloc area.
	 */
	movi	a0, init_mm
	j	8b

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

2:	/* Special case for cache aliasing.
	 * We (should) only get here if a clear_user_page, copy_user_page
	 * or the aliased cache flush functions got preemptively interrupted 
	 * by another task. Re-establish temporary mapping to the 
	 * TLBTEMP_BASE areas.
	 */

	/* We shouldn't be in a double exception */

	l32i	a0, a2, PT_DEPC
	bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, 2f

	/* Make sure the exception originated in the special functions */

	movi	a0, __tlbtemp_mapping_start
	rsr	a3, epc1
	bltu	a3, a0, 2f
	movi	a0, __tlbtemp_mapping_end
	bgeu	a3, a0, 2f

	/* Check if excvaddr was in one of the TLBTEMP_BASE areas. */

	movi	a3, TLBTEMP_BASE_1
	rsr	a0, excvaddr
	bltu	a0, a3, 2f

	addi	a1, a0, -TLBTEMP_SIZE
	bgeu	a1, a3, 2f

	/* Check if we have to restore an ITLB mapping. */

	movi	a1, __tlbtemp_mapping_itlb
	rsr	a3, epc1
	sub	a3, a3, a1

	/* Calculate VPN */

	movi	a1, PAGE_MASK
	and	a1, a1, a0

	/* Jump for ITLB entry */

	bgez	a3, 1f

	/* We can use up to two TLBTEMP areas, one for src and one for dst. */

	extui	a3, a0, PAGE_SHIFT + DCACHE_ALIAS_ORDER, 1
	add	a1, a3, a1

	/* PPN is in a6 for the first TLBTEMP area and in a7 for the second. */

	mov	a0, a6
	movnez	a0, a7, a3
	j	3b

	/* ITLB entry. We only use dst in a6. */

1:	witlb	a6, a1
	isync
	j	4b


#endif	// DCACHE_WAY_SIZE > PAGE_SIZE


2:	/* Invalid PGD, default exception handling */

	rsr	a1, depc
	s32i	a1, a2, PT_AREG2
	mov	a1, a2

	rsr	a2, ps
	bbsi.l	a2, PS_UM_BIT, 1f
	j	_kernel_exception
1:	j	_user_exception

ENDPROC(fast_second_level_miss)

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
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 *   PT_DEPC >= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception, DEPC
 *	     <  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 */

ENTRY(fast_store_prohibited)

	/* Save a1 and a3. */

	s32i	a1, a2, PT_AREG1
	s32i	a3, a2, PT_AREG3

	GET_CURRENT(a1,a2)
	l32i	a0, a1, TASK_MM		# tsk->mm
	beqz	a0, 9f

8:	rsr	a1, excvaddr		# fault address
	_PGD_OFFSET(a0, a1, a3)
	l32i	a0, a0, 0
	beqz	a0, 2f

	/*
	 * Note that we test _PAGE_WRITABLE_BIT only if PTE is present
	 * and is not PAGE_NONE. See pgtable.h for possible PTE layouts.
	 */

	_PTE_OFFSET(a0, a1, a3)
	l32i	a3, a0, 0		# read pteval
	movi	a1, _PAGE_CA_INVALID
	ball	a3, a1, 2f
	bbci.l	a3, _PAGE_WRITABLE_BIT, 2f

	movi	a1, _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_HW_WRITE
	or	a3, a3, a1
	rsr	a1, excvaddr
	s32i	a3, a0, 0

	/* We need to flush the cache if we have page coloring. */
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	dhwb	a0, 0
#endif
	pdtlb	a0, a1
	wdtlb	a3, a0

	/* Exit critical section. */

	movi	a0, 0
	rsr	a3, excsave1
	s32i	a0, a3, EXC_TABLE_FIXUP

	/* Restore the working registers, and return. */

	l32i	a3, a2, PT_AREG3
	l32i	a1, a2, PT_AREG1
	l32i	a0, a2, PT_AREG0
	l32i	a2, a2, PT_DEPC

	bgeui	a2, VALID_DOUBLE_EXCEPTION_ADDRESS, 1f

	rsr	a2, depc
	rfe

	/* Double exception. Restore FIXUP handler and return. */

1:	xsr	a2, depc
	esync
	rfde

9:	l32i	a0, a1, TASK_ACTIVE_MM	# unlikely case mm == 0
	j	8b

2:	/* If there was a problem, handle fault in C */

	rsr	a3, depc	# still holds a2
	s32i	a3, a2, PT_AREG2
	mov	a1, a2

	rsr	a2, ps
	bbsi.l	a2, PS_UM_BIT, 1f
	j	_kernel_exception
1:	j	_user_exception

ENDPROC(fast_store_prohibited)

#endif /* CONFIG_MMU */

/*
 * System Calls.
 *
 * void system_call (struct pt_regs* regs, int exccause)
 *                            a2                 a3
 */
	.literal_position

ENTRY(system_call)

	abi_entry_default

	/* regs->syscall = regs->areg[2] */

	l32i	a7, a2, PT_AREG2
	s32i	a7, a2, PT_SYSCALL

	GET_THREAD_INFO(a4, a1)
	l32i	a3, a4, TI_FLAGS
	movi	a4, _TIF_WORK_MASK
	and	a3, a3, a4
	beqz	a3, 1f

	mov	a6, a2
	call4	do_syscall_trace_enter
	beqz	a6, .Lsyscall_exit
	l32i	a7, a2, PT_SYSCALL

1:
	/* syscall = sys_call_table[syscall_nr] */

	movi	a4, sys_call_table
	movi	a5, __NR_syscalls
	movi	a6, -ENOSYS
	bgeu	a7, a5, 1f

	addx4	a4, a7, a4
	l32i	a4, a4, 0

	/* Load args: arg0 - arg5 are passed via regs. */

	l32i	a6, a2, PT_AREG6
	l32i	a7, a2, PT_AREG3
	l32i	a8, a2, PT_AREG4
	l32i	a9, a2, PT_AREG5
	l32i	a10, a2, PT_AREG8
	l32i	a11, a2, PT_AREG9

	callx4	a4

1:	/* regs->areg[2] = return_value */

	s32i	a6, a2, PT_AREG2
	bnez	a3, 1f
.Lsyscall_exit:
	abi_ret_default

1:
	mov	a6, a2
	call4	do_syscall_trace_leave
	abi_ret_default

ENDPROC(system_call)

/*
 * Spill live registers on the kernel stack macro.
 *
 * Entry condition: ps.woe is set, ps.excm is cleared
 * Exit condition: windowstart has single bit set
 * May clobber: a12, a13
 */
	.macro	spill_registers_kernel

#if XCHAL_NUM_AREGS > 16
	call12	1f
	_j	2f
	retw
	.align	4
1:
	_entry	a1, 48
	addi	a12, a0, 3
#if XCHAL_NUM_AREGS > 32
	.rept	(XCHAL_NUM_AREGS - 32) / 12
	_entry	a1, 48
	mov	a12, a0
	.endr
#endif
	_entry	a1, 16
#if XCHAL_NUM_AREGS % 12 == 0
	mov	a8, a8
#elif XCHAL_NUM_AREGS % 12 == 4
	mov	a12, a12
#elif XCHAL_NUM_AREGS % 12 == 8
	mov	a4, a4
#endif
	retw
2:
#else
	mov	a12, a12
#endif
	.endm

/*
 * Task switch.
 *
 * struct task*  _switch_to (struct task* prev, struct task* next)
 *         a2                              a2                 a3
 */

ENTRY(_switch_to)

	abi_entry(XTENSA_SPILL_STACK_RESERVE)

	mov	a11, a3			# and 'next' (a3)

	l32i	a4, a2, TASK_THREAD_INFO
	l32i	a5, a3, TASK_THREAD_INFO

	save_xtregs_user a4 a6 a8 a9 a12 a13 THREAD_XTREGS_USER

#if THREAD_RA > 1020 || THREAD_SP > 1020
	addi	a10, a2, TASK_THREAD
	s32i	a0, a10, THREAD_RA - TASK_THREAD	# save return address
	s32i	a1, a10, THREAD_SP - TASK_THREAD	# save stack pointer
#else
	s32i	a0, a2, THREAD_RA	# save return address
	s32i	a1, a2, THREAD_SP	# save stack pointer
#endif

#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	movi	a6, __stack_chk_guard
	l32i	a8, a3, TASK_STACK_CANARY
	s32i	a8, a6, 0
#endif

	/* Disable ints while we manipulate the stack pointer. */

	irq_save a14, a3
	rsync

	/* Switch CPENABLE */

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	l32i	a3, a5, THREAD_CPENABLE
	xsr	a3, cpenable
	s32i	a3, a4, THREAD_CPENABLE
#endif

	/* Flush register file. */

	spill_registers_kernel

	/* Set kernel stack (and leave critical section)
	 * Note: It's save to set it here. The stack will not be overwritten
	 *       because the kernel stack will only be loaded again after
	 *       we return from kernel space.
	 */

	rsr	a3, excsave1		# exc_table
	addi	a7, a5, PT_REGS_OFFSET
	s32i	a7, a3, EXC_TABLE_KSTK

	/* restore context of the task 'next' */

	l32i	a0, a11, THREAD_RA	# restore return address
	l32i	a1, a11, THREAD_SP	# restore stack pointer

	load_xtregs_user a5 a6 a8 a9 a12 a13 THREAD_XTREGS_USER

	wsr	a14, ps
	rsync

	abi_ret(XTENSA_SPILL_STACK_RESERVE)

ENDPROC(_switch_to)

ENTRY(ret_from_fork)

	/* void schedule_tail (struct task_struct *prev)
	 * Note: prev is still in a6 (return value from fake call4 frame)
	 */
	call4	schedule_tail

	mov	a6, a1
	call4	do_syscall_trace_leave

	j	common_exception_return

ENDPROC(ret_from_fork)

/*
 * Kernel thread creation helper
 * On entry, set up by copy_thread: a2 = thread_fn, a3 = thread_fn arg
 *           left from _switch_to: a6 = prev
 */
ENTRY(ret_from_kernel_thread)

	call4	schedule_tail
	mov	a6, a3
	callx4	a2
	j	common_exception_return

ENDPROC(ret_from_kernel_thread)
