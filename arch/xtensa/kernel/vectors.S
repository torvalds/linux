/*
 * arch/xtensa/kernel/vectors.S
 *
 * This file contains all exception vectors (user, kernel, and double),
 * as well as the window vectors (overflow and underflow), and the debug
 * vector. These are the primary vectors executed by the processor if an
 * exception occurs.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2005 - 2008 Tensilica, Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 */

/*
 * We use a two-level table approach. The user and kernel exception vectors
 * use a first-level dispatch table to dispatch the exception to a registered
 * fast handler or the default handler, if no fast handler was registered.
 * The default handler sets up a C-stack and dispatches the exception to a
 * registerd C handler in the second-level dispatch table.
 *
 * Fast handler entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original value in depc
 *   a3:	dispatch table
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	a3
 *
 * The value for PT_DEPC saved to stack also functions as a boolean to
 * indicate that the exception is either a double or a regular exception:
 *
 *   PT_DEPC	>= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception
 *		<  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Note:  Neither the kernel nor the user exception handler generate literals.
 *
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/vectors.h>

#define WINDOW_VECTORS_SIZE   0x180


/*
 * User exception vector. (Exceptions with PS.UM == 1, PS.EXCM == 0)
 *
 * We get here when an exception occurred while we were in userland.
 * We switch to the kernel stack and jump to the first level handler
 * associated to the exception cause.
 *
 * Note: the saved kernel stack pointer (EXC_TABLE_KSTK) is already
 *       decremented by PT_USER_SIZE.
 */

	.section .UserExceptionVector.text, "ax"

ENTRY(_UserExceptionVector)

	xsr	a3, excsave1		# save a3 and get dispatch table
	wsr	a2, depc		# save a2
	l32i	a2, a3, EXC_TABLE_KSTK	# load kernel stack to a2
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, exccause		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER	# load handler
	xsr	a3, excsave1		# restore a3 and dispatch table
	jx	a0

ENDPROC(_UserExceptionVector)

/*
 * Kernel exception vector. (Exceptions with PS.UM == 0, PS.EXCM == 0)
 *
 * We get this exception when we were already in kernel space.
 * We decrement the current stack pointer (kernel) by PT_SIZE and
 * jump to the first-level handler associated with the exception cause.
 *
 * Note: we need to preserve space for the spill region.
 */

	.section .KernelExceptionVector.text, "ax"

ENTRY(_KernelExceptionVector)

	xsr	a3, excsave1		# save a3, and get dispatch table
	wsr	a2, depc		# save a2
	addi	a2, a1, -16-PT_SIZE	# adjust stack pointer
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, exccause		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_KERNEL	# load handler address
	xsr	a3, excsave1		# restore a3 and dispatch table
	jx	a0

ENDPROC(_KernelExceptionVector)

/*
 * Double exception vector (Exceptions with PS.EXCM == 1)
 * We get this exception when another exception occurs while were are
 * already in an exception, such as window overflow/underflow exception,
 * or 'expected' exceptions, for example memory exception when we were trying
 * to read data from an invalid address in user space.
 *
 * Note that this vector is never invoked for level-1 interrupts, because such
 * interrupts are disabled (masked) when PS.EXCM is set.
 *
 * We decode the exception and take the appropriate action.  However, the
 * double exception vector is much more careful, because a lot more error
 * cases go through the double exception vector than through the user and
 * kernel exception vectors.
 *
 * Occasionally, the kernel expects a double exception to occur.  This usually
 * happens when accessing user-space memory with the user's permissions
 * (l32e/s32e instructions).  The kernel state, though, is not always suitable
 * for immediate transfer of control to handle_double, where "normal" exception
 * processing occurs. Also in kernel mode, TLB misses can occur if accessing
 * vmalloc memory, possibly requiring repair in a double exception handler.
 *
 * The variable at TABLE_FIXUP offset from the pointer in EXCSAVE_1 doubles as
 * a boolean variable and a pointer to a fixup routine. If the variable
 * EXC_TABLE_FIXUP is non-zero, this handler jumps to that address. A value of
 * zero indicates to use the default kernel/user exception handler.
 * There is only one exception, when the value is identical to the exc_table
 * label, the kernel is in trouble. This mechanism is used to protect critical
 * sections, mainly when the handler writes to the stack to assert the stack
 * pointer is valid. Once the fixup/default handler leaves that area, the
 * EXC_TABLE_FIXUP variable is reset to the fixup handler or zero.
 *
 * Procedures wishing to use this mechanism should set EXC_TABLE_FIXUP to the
 * nonzero address of a fixup routine before it could cause a double exception
 * and reset it before it returns.
 *
 * Some other things to take care of when a fast exception handler doesn't
 * specify a particular fixup handler but wants to use the default handlers:
 *
 *  - The original stack pointer (in a1) must not be modified. The fast
 *    exception handler should only use a2 as the stack pointer.
 *
 *  - If the fast handler manipulates the stack pointer (in a2), it has to
 *    register a valid fixup handler and cannot use the default handlers.
 *
 *  - The handler can use any other generic register from a3 to a15, but it
 *    must save the content of these registers to stack (PT_AREG3...PT_AREGx)
 *
 *  - These registers must be saved before a double exception can occur.
 *
 *  - If we ever implement handling signals while in double exceptions, the
 *    number of registers a fast handler has saved (excluding a0 and a1) must
 *    be written to  PT_AREG1. (1 if only a3 is used, 2 for a3 and a4, etc. )
 *
 * The fixup handlers are special handlers:
 *
 *  - Fixup entry conditions differ from regular exceptions:
 *
 *	a0:	   DEPC
 *	a1: 	   a1
 *	a2:	   trashed, original value in EXC_TABLE_DOUBLE_SAVE
 *	a3:	   exctable
 *	depc:	   a0
 *	excsave_1: a3
 *
 *  - When the kernel enters the fixup handler, it still assumes it is in a
 *    critical section, so EXC_TABLE_FIXUP variable is set to exc_table.
 *    The fixup handler, therefore, has to re-register itself as the fixup
 *    handler before it returns from the double exception.
 *
 *  - Fixup handler can share the same exception frame with the fast handler.
 *    The kernel stack pointer is not changed when entering the fixup handler.
 *
 *  - Fixup handlers can jump to the default kernel and user exception
 *    handlers. Before it jumps, though, it has to setup a exception frame
 *    on stack. Because the default handler resets the register fixup handler
 *    the fixup handler must make sure that the default handler returns to
 *    it instead of the exception address, so it can re-register itself as
 *    the fixup handler.
 *
 * In case of a critical condition where the kernel cannot recover, we jump
 * to unrecoverable_exception with the following entry conditions.
 * All registers a0...a15 are unchanged from the last exception, except:
 *
 *	a0:	   last address before we jumped to the unrecoverable_exception.
 *	excsave_1: a0
 *
 *
 * See the handle_alloca_user and spill_registers routines for example clients.
 *
 * FIXME: Note: we currently don't allow signal handling coming from a double
 *        exception, so the item markt with (*) is not required.
 */

	.section .DoubleExceptionVector.text, "ax"
	.begin literal_prefix .DoubleExceptionVector
	.globl _DoubleExceptionVector_WindowUnderflow
	.globl _DoubleExceptionVector_WindowOverflow

ENTRY(_DoubleExceptionVector)

	xsr	a3, excsave1
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE

	/* Check for kernel double exception (usually fatal). */

	rsr	a2, ps
	_bbci.l	a2, PS_UM_BIT, .Lksp

	/* Check if we are currently handling a window exception. */
	/* Note: We don't need to indicate that we enter a critical section. */

	xsr	a0, depc		# get DEPC, save a0

	movi	a2, WINDOW_VECTORS_VADDR
	_bltu	a0, a2, .Lfixup
	addi	a2, a2, WINDOW_VECTORS_SIZE
	_bgeu	a0, a2, .Lfixup

	/* Window overflow/underflow exception. Get stack pointer. */

	l32i	a2, a3, EXC_TABLE_KSTK

	/* Check for overflow/underflow exception, jump if overflow. */

	bbci.l	a0, 6, _DoubleExceptionVector_WindowOverflow

	/*
	 * Restart window underflow exception.
	 * Currently:
	 *	depc = orig a0,
	 *	a0 = orig DEPC,
	 *	a2 = new sp based on KSTK from exc_table
	 *	a3 = excsave_1
	 *	excsave_1 = orig a3
	 *
	 * We return to the instruction in user space that caused the window
	 * underflow exception. Therefore, we change window base to the value
	 * before we entered the window underflow exception and prepare the
	 * registers to return as if we were coming from a regular exception
	 * by changing depc (in a0).
	 * Note: We can trash the current window frame (a0...a3) and depc!
	 */
_DoubleExceptionVector_WindowUnderflow:
	xsr	a3, excsave1
	wsr	a2, depc		# save stack pointer temporarily
	rsr	a0, ps
	extui	a0, a0, PS_OWB_SHIFT, PS_OWB_WIDTH
	wsr	a0, windowbase
	rsync

	/* We are now in the previous window frame. Save registers again. */

	xsr	a2, depc		# save a2 and get stack pointer
	s32i	a0, a2, PT_AREG0
	xsr	a3, excsave1
	rsr	a0, exccause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3
	xsr	a3, excsave1
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0

	/*
	 * We only allow the ITLB miss exception if we are in kernel space.
	 * All other exceptions are unexpected and thus unrecoverable!
	 */

#ifdef CONFIG_MMU
	.extern fast_second_level_miss_double_kernel

.Lksp:	/* a0: a0, a1: a1, a2: a2, a3: trashed, depc: depc, excsave: a3 */

	rsr	a3, exccause
	beqi	a3, EXCCAUSE_ITLB_MISS, 1f
	addi	a3, a3, -EXCCAUSE_DTLB_MISS
	bnez	a3, .Lunrecoverable
1:	movi	a3, fast_second_level_miss_double_kernel
	jx	a3
#else
.equ	.Lksp,	.Lunrecoverable
#endif

	/* Critical! We can't handle this situation. PANIC! */

	.extern unrecoverable_exception

.Lunrecoverable_fixup:
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a0, depc

.Lunrecoverable:
	rsr	a3, excsave1
	wsr	a0, excsave1
	movi	a0, unrecoverable_exception
	callx0	a0

.Lfixup:/* Check for a fixup handler or if we were in a critical section. */

	/* a0: depc, a1: a1, a2: trash, a3: exctable, depc: a0, excsave1: a3 */

	/* Enter critical section. */

	l32i	a2, a3, EXC_TABLE_FIXUP
	s32i	a3, a3, EXC_TABLE_FIXUP
	beq	a2, a3, .Lunrecoverable_fixup	# critical section
	beqz	a2, .Ldflt			# no handler was registered

	/* a0: depc, a1: a1, a2: trash, a3: exctable, depc: a0, excsave: a3 */

	jx	a2

.Ldflt:	/* Get stack pointer. */

	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	addi	a2, a2, -PT_USER_SIZE

	/* a0: depc, a1: a1, a2: kstk, a3: exctable, depc: a0, excsave: a3 */

	s32i	a0, a2, PT_DEPC
	l32i	a0, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a0, depc
	s32i	a0, a2, PT_AREG0

	/* a0: avail, a1: a1, a2: kstk, a3: exctable, depc: a2, excsave: a3 */

	rsr	a0, exccause
	addx4	a0, a0, a3
	xsr	a3, excsave1
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0

	/*
	 * Restart window OVERFLOW exception.
	 * Currently:
	 *	depc = orig a0,
	 *	a0 = orig DEPC,
	 *	a2 = new sp based on KSTK from exc_table
	 *	a3 = EXCSAVE_1
	 *	excsave_1 = orig a3
	 *
	 * We return to the instruction in user space that caused the window
	 * overflow exception. Therefore, we change window base to the value
	 * before we entered the window overflow exception and prepare the
	 * registers to return as if we were coming from a regular exception
	 * by changing DEPC (in a0).
	 *
	 * NOTE: We CANNOT trash the current window frame (a0...a3), but we
	 * can clobber depc.
	 *
	 * The tricky part here is that overflow8 and overflow12 handlers
	 * save a0, then clobber a0.  To restart the handler, we have to restore
	 * a0 if the double exception was past the point where a0 was clobbered.
	 *
	 * To keep things simple, we take advantage of the fact all overflow
	 * handlers save a0 in their very first instruction.  If DEPC was past
	 * that instruction, we can safely restore a0 from where it was saved
	 * on the stack.
	 *
	 * a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3
	 */
_DoubleExceptionVector_WindowOverflow:
	extui	a2, a0, 0, 6	# get offset into 64-byte vector handler
	beqz	a2, 1f		# if at start of vector, don't restore

	addi	a0, a0, -128
	bbsi.l	a0, 8, 1f	# don't restore except for overflow 8 and 12

	/*
	 * This fixup handler is for the extremely unlikely case where the
	 * overflow handler's reference thru a0 gets a hardware TLB refill
	 * that bumps out the (distinct, aliasing) TLB entry that mapped its
	 * prior references thru a9/a13, and where our reference now thru
	 * a9/a13 gets a 2nd-level miss exception (not hardware TLB refill).
	 */
	movi	a2, window_overflow_restore_a0_fixup
	s32i	a2, a3, EXC_TABLE_FIXUP
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a3, excsave1

	bbsi.l	a0, 7, 2f

	/*
	 * Restore a0 as saved by _WindowOverflow8().
	 */

	l32e	a0, a9, -16
	wsr	a0, depc	# replace the saved a0
	j	3f

2:
	/*
	 * Restore a0 as saved by _WindowOverflow12().
	 */

	l32e	a0, a13, -16
	wsr	a0, depc	# replace the saved a0
3:
	xsr	a3, excsave1
	movi	a0, 0
	s32i	a0, a3, EXC_TABLE_FIXUP
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
1:
	/*
	 * Restore WindowBase while leaving all address registers restored.
	 * We have to use ROTW for this, because WSR.WINDOWBASE requires
	 * an address register (which would prevent restore).
	 *
	 * Window Base goes from 0 ... 7 (Module 8)
	 * Window Start is 8 bits; Ex: (0b1010 1010):0x55 from series of call4s
	 */

	rsr	a0, ps
	extui	a0, a0, PS_OWB_SHIFT, PS_OWB_WIDTH
	rsr	a2, windowbase
	sub	a0, a2, a0
	extui	a0, a0, 0, 3

	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a3, excsave1
	beqi	a0, 1, .L1pane
	beqi	a0, 3, .L3pane

	rsr	a0, depc
	rotw	-2

	/*
	 * We are now in the user code's original window frame.
	 * Process the exception as a user exception as if it was
	 * taken by the user code.
	 *
	 * This is similar to the user exception vector,
	 * except that PT_DEPC isn't set to EXCCAUSE.
	 */
1:
	xsr	a3, excsave1
	wsr	a2, depc
	l32i	a2, a3, EXC_TABLE_KSTK
	s32i	a0, a2, PT_AREG0
	rsr	a0, exccause

	s32i	a0, a2, PT_DEPC

_DoubleExceptionVector_handle_exception:
	addi	a0, a0, -EXCCAUSE_UNALIGNED
	beqz	a0, 2f
	addx4	a0, a0, a3
	l32i	a0, a0, EXC_TABLE_FAST_USER + 4 * EXCCAUSE_UNALIGNED
	xsr	a3, excsave1
	jx	a0
2:
	movi	a0, user_exception
	xsr	a3, excsave1
	jx	a0

.L1pane:
	rsr	a0, depc
	rotw	-1
	j	1b

.L3pane:
	rsr	a0, depc
	rotw	-3
	j	1b


ENDPROC(_DoubleExceptionVector)

	.end literal_prefix

	.text
/*
 * Fixup handler for TLB miss in double exception handler for window owerflow.
 * We get here with windowbase set to the window that was being spilled and
 * a0 trashed. a0 bit 7 determines if this is a call8 (bit clear) or call12
 * (bit set) window.
 *
 * We do the following here:
 * - go to the original window retaining a0 value;
 * - set up exception stack to return back to appropriate a0 restore code
 *   (we'll need to rotate window back and there's no place to save this
 *    information, use different return address for that);
 * - handle the exception;
 * - go to the window that was being spilled;
 * - set up window_overflow_restore_a0_fixup as a fixup routine;
 * - reload a0;
 * - restore the original window;
 * - reset the default fixup routine;
 * - return to user. By the time we get to this fixup handler all information
 *   about the conditions of the original double exception that happened in
 *   the window overflow handler is lost, so we just return to userspace to
 *   retry overflow from start.
 *
 * a0: value of depc, original value in depc
 * a2: trashed, original value in EXC_TABLE_DOUBLE_SAVE
 * a3: exctable, original value in excsave1
 */

ENTRY(window_overflow_restore_a0_fixup)

	rsr	a0, ps
	extui	a0, a0, PS_OWB_SHIFT, PS_OWB_WIDTH
	rsr	a2, windowbase
	sub	a0, a2, a0
	extui	a0, a0, 0, 3
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a3, excsave1

	_beqi	a0, 1, .Lhandle_1
	_beqi	a0, 3, .Lhandle_3

	.macro	overflow_fixup_handle_exception_pane n

	rsr	a0, depc
	rotw	-\n

	xsr	a3, excsave1
	wsr	a2, depc
	l32i	a2, a3, EXC_TABLE_KSTK
	s32i	a0, a2, PT_AREG0

	movi	a0, .Lrestore_\n
	s32i	a0, a2, PT_DEPC
	rsr	a0, exccause
	j	_DoubleExceptionVector_handle_exception

	.endm

	overflow_fixup_handle_exception_pane 2
.Lhandle_1:
	overflow_fixup_handle_exception_pane 1
.Lhandle_3:
	overflow_fixup_handle_exception_pane 3

	.macro	overflow_fixup_restore_a0_pane n

	rotw	\n
	/* Need to preserve a0 value here to be able to handle exception
	 * that may occur on a0 reload from stack. It may occur because
	 * TLB miss handler may not be atomic and pointer to page table
	 * may be lost before we get here. There are no free registers,
	 * so we need to use EXC_TABLE_DOUBLE_SAVE area.
	 */
	xsr	a3, excsave1
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	movi	a2, window_overflow_restore_a0_fixup
	s32i	a2, a3, EXC_TABLE_FIXUP
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a3, excsave1
	bbsi.l	a0, 7, 1f
	l32e	a0, a9, -16
	j	2f
1:
	l32e	a0, a13, -16
2:
	rotw	-\n

	.endm

.Lrestore_2:
	overflow_fixup_restore_a0_pane 2

.Lset_default_fixup:
	xsr	a3, excsave1
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	movi	a2, 0
	s32i	a2, a3, EXC_TABLE_FIXUP
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a3, excsave1
	rfe

.Lrestore_1:
	overflow_fixup_restore_a0_pane 1
	j	.Lset_default_fixup
.Lrestore_3:
	overflow_fixup_restore_a0_pane 3
	j	.Lset_default_fixup

ENDPROC(window_overflow_restore_a0_fixup)

/*
 * Debug interrupt vector
 *
 * There is not much space here, so simply jump to another handler.
 * EXCSAVE[DEBUGLEVEL] has been set to that handler.
 */

	.section .DebugInterruptVector.text, "ax"

ENTRY(_DebugInterruptVector)

	xsr	a3, SREG_EXCSAVE + XCHAL_DEBUGLEVEL
	s32i	a0, a3, DT_DEBUG_SAVE
	l32i	a0, a3, DT_DEBUG_EXCEPTION
	jx	a0

ENDPROC(_DebugInterruptVector)



/*
 * Medium priority level interrupt vectors
 *
 * Each takes less than 16 (0x10) bytes, no literals, by placing
 * the extra 8 bytes that would otherwise be required in the window
 * vectors area where there is space.  With relocatable vectors,
 * all vectors are within ~ 4 kB range of each other, so we can
 * simply jump (J) to another vector without having to use JX.
 *
 * common_exception code gets current IRQ level in PS.INTLEVEL
 * and preserves it for the IRQ handling time.
 */

	.macro	irq_entry_level level

	.if	XCHAL_EXCM_LEVEL >= \level
	.section .Level\level\()InterruptVector.text, "ax"
ENTRY(_Level\level\()InterruptVector)
	wsr	a0, excsave2
	rsr	a0, epc\level
	wsr	a0, epc1
	.if	\level <= LOCKLEVEL
	movi	a0, EXCCAUSE_LEVEL1_INTERRUPT
	.else
	movi	a0, EXCCAUSE_MAPPED_NMI
	.endif
	wsr	a0, exccause
	rsr	a0, eps\level
					# branch to user or kernel vector
	j	_SimulateUserKernelVectorException
	.endif

	.endm

	irq_entry_level 2
	irq_entry_level 3
	irq_entry_level 4
	irq_entry_level 5
	irq_entry_level 6


/* Window overflow and underflow handlers.
 * The handlers must be 64 bytes apart, first starting with the underflow
 * handlers underflow-4 to underflow-12, then the overflow handlers
 * overflow-4 to overflow-12.
 *
 * Note: We rerun the underflow handlers if we hit an exception, so
 *	 we try to access any page that would cause a page fault early.
 */

#define ENTRY_ALIGN64(name)	\
	.globl name;		\
	.align 64;		\
	name:

	.section		.WindowVectors.text, "ax"


/* 4-Register Window Overflow Vector (Handler) */

ENTRY_ALIGN64(_WindowOverflow4)

	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5,  -8
	s32e	a3, a5,  -4
	rfwo

ENDPROC(_WindowOverflow4)


#if XCHAL_EXCM_LEVEL >= 2
	/*  Not a window vector - but a convenient location
	 *  (where we know there's space) for continuation of
	 *  medium priority interrupt dispatch code.
	 *  On entry here, a0 contains PS, and EPC2 contains saved a0:
	 */
	.align 4
_SimulateUserKernelVectorException:
	addi	a0, a0, (1 << PS_EXCM_BIT)
#if !XTENSA_FAKE_NMI
	wsr	a0, ps
#endif
	bbsi.l	a0, PS_UM_BIT, 1f	# branch if user mode
	xsr	a0, excsave2		# restore a0
	j	_KernelExceptionVector	# simulate kernel vector exception
1:	xsr	a0, excsave2		# restore a0
	j	_UserExceptionVector	# simulate user vector exception
#endif


/* 4-Register Window Underflow Vector (Handler) */

ENTRY_ALIGN64(_WindowUnderflow4)

	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5,  -8
	l32e	a3, a5,  -4
	rfwu

ENDPROC(_WindowUnderflow4)

/* 8-Register Window Overflow Vector (Handler) */

ENTRY_ALIGN64(_WindowOverflow8)

	s32e	a0, a9, -16
	l32e	a0, a1, -12
	s32e	a2, a9,  -8
	s32e	a1, a9, -12
	s32e	a3, a9,  -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rfwo

ENDPROC(_WindowOverflow8)

/* 8-Register Window Underflow Vector (Handler) */

ENTRY_ALIGN64(_WindowUnderflow8)

	l32e	a1, a9, -12
	l32e	a0, a9, -16
	l32e	a7, a1, -12
	l32e	a2, a9,  -8
	l32e	a4, a7, -32
	l32e	a3, a9,  -4
	l32e	a5, a7, -28
	l32e	a6, a7, -24
	l32e	a7, a7, -20
	rfwu

ENDPROC(_WindowUnderflow8)

/* 12-Register Window Overflow Vector (Handler) */

ENTRY_ALIGN64(_WindowOverflow12)

	s32e	a0,  a13, -16
	l32e	a0,  a1,  -12
	s32e	a1,  a13, -12
	s32e	a2,  a13,  -8
	s32e	a3,  a13,  -4
	s32e	a4,  a0,  -48
	s32e	a5,  a0,  -44
	s32e	a6,  a0,  -40
	s32e	a7,  a0,  -36
	s32e	a8,  a0,  -32
	s32e	a9,  a0,  -28
	s32e	a10, a0,  -24
	s32e	a11, a0,  -20
	rfwo

ENDPROC(_WindowOverflow12)

/* 12-Register Window Underflow Vector (Handler) */

ENTRY_ALIGN64(_WindowUnderflow12)

	l32e	a1,  a13, -12
	l32e	a0,  a13, -16
	l32e	a11, a1,  -12
	l32e	a2,  a13,  -8
	l32e	a4,  a11, -48
	l32e	a8,  a11, -32
	l32e	a3,  a13,  -4
	l32e	a5,  a11, -44
	l32e	a6,  a11, -40
	l32e	a7,  a11, -36
	l32e	a9,  a11, -28
	l32e	a10, a11, -24
	l32e	a11, a11, -20
	rfwu

ENDPROC(_WindowUnderflow12)

	.text
