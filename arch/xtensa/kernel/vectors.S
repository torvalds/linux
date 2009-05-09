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
 * Copyright (C) 2005 Tensilica, Inc.
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
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/processor.h>

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

	xsr	a3, EXCSAVE_1		# save a3 and get dispatch table
	wsr	a2, DEPC		# save a2
	l32i	a2, a3, EXC_TABLE_KSTK	# load kernel stack to a2
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, EXCCAUSE		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER	# load handler
	jx	a0

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

	xsr	a3, EXCSAVE_1		# save a3, and get dispatch table
	wsr	a2, DEPC		# save a2
	addi	a2, a1, -16-PT_SIZE	# adjust stack pointer
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, EXCCAUSE		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_KERNEL	# load handler address
	jx	a0


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
 *	a2:	   trashed, original value in EXC_TABLE_DOUBLE_A2
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

ENTRY(_DoubleExceptionVector)

	/* Deliberately destroy excsave (don't assume it's value was valid). */

	wsr	a3, EXCSAVE_1		# save a3

	/* Check for kernel double exception (usually fatal). */

	rsr	a3, PS
	_bbci.l	a3, PS_UM_BIT, .Lksp

	/* Check if we are currently handling a window exception. */
	/* Note: We don't need to indicate that we enter a critical section. */

	xsr	a0, DEPC		# get DEPC, save a0

	movi	a3, XCHAL_WINDOW_VECTORS_VADDR
	_bltu	a0, a3, .Lfixup
	addi	a3, a3, WINDOW_VECTORS_SIZE
	_bgeu	a0, a3, .Lfixup

	/* Window overflow/underflow exception. Get stack pointer. */

	mov	a3, a2
	movi	a2, exc_table
	l32i	a2, a2, EXC_TABLE_KSTK

	/* Check for overflow/underflow exception, jump if overflow. */

	_bbci.l	a0, 6, .Lovfl

	/* a0: depc, a1: a1, a2: kstk, a3: a2, depc: a0, excsave: a3  */

	/* Restart window underflow exception.
	 * We return to the instruction in user space that caused the window
	 * underflow exception. Therefore, we change window base to the value
	 * before we entered the window underflow exception and prepare the
	 * registers to return as if we were coming from a regular exception
	 * by changing depc (in a0).
	 * Note: We can trash the current window frame (a0...a3) and depc!
	 */

	wsr	a2, DEPC		# save stack pointer temporarily
	rsr	a0, PS
	extui	a0, a0, PS_OWB_SHIFT, 4
	wsr	a0, WINDOWBASE
	rsync

	/* We are now in the previous window frame. Save registers again. */

	xsr	a2, DEPC		# save a2 and get stack pointer
	s32i	a0, a2, PT_AREG0

	wsr	a3, EXCSAVE_1		# save a3
	movi	a3, exc_table

	rsr	a0, EXCCAUSE
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0

.Lfixup:/* Check for a fixup handler or if we were in a critical section. */

	/* a0: depc, a1: a1, a2: a2, a3: trashed, depc: a0, excsave1: a3 */

	movi	a3, exc_table
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE	# temporary variable

	/* Enter critical section. */

	l32i	a2, a3, EXC_TABLE_FIXUP
	s32i	a3, a3, EXC_TABLE_FIXUP
	beq	a2, a3, .Lunrecoverable_fixup	# critical!
	beqz	a2, .Ldflt			# no handler was registered

	/* a0: depc, a1: a1, a2: trash, a3: exctable, depc: a0, excsave: a3 */

	jx	a2

.Ldflt:	/* Get stack pointer. */

	l32i	a3, a3, EXC_TABLE_DOUBLE_SAVE
	addi	a2, a3, -PT_USER_SIZE

.Lovfl:	/* Jump to default handlers. */

	/* a0: depc, a1: a1, a2: kstk, a3: a2, depc: a0, excsave: a3 */

	xsr	a3, DEPC
	s32i	a0, a2, PT_DEPC
	s32i	a3, a2, PT_AREG0

	/* a0: avail, a1: a1, a2: kstk, a3: avail, depc: a2, excsave: a3 */

	movi	a3, exc_table
	rsr	a0, EXCCAUSE
	addx4	a0, a0, a3
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0

	/*
	 * We only allow the ITLB miss exception if we are in kernel space.
	 * All other exceptions are unexpected and thus unrecoverable!
	 */

#ifdef CONFIG_MMU
	.extern fast_second_level_miss_double_kernel

.Lksp:	/* a0: a0, a1: a1, a2: a2, a3: trashed, depc: depc, excsave: a3 */

	rsr	a3, EXCCAUSE
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
	xsr	a0, DEPC

.Lunrecoverable:
	rsr	a3, EXCSAVE_1
	wsr	a0, EXCSAVE_1
	movi	a0, unrecoverable_exception
	callx0	a0

	.end literal_prefix


/*
 * Debug interrupt vector
 *
 * There is not much space here, so simply jump to another handler.
 * EXCSAVE[DEBUGLEVEL] has been set to that handler.
 */

	.section .DebugInterruptVector.text, "ax"

ENTRY(_DebugInterruptVector)
	xsr	a0, EXCSAVE + XCHAL_DEBUGLEVEL
	jx	a0



/* Window overflow and underflow handlers.
 * The handlers must be 64 bytes apart, first starting with the underflow
 * handlers underflow-4 to underflow-12, then the overflow handlers
 * overflow-4 to overflow-12.
 *
 * Note: We rerun the underflow handlers if we hit an exception, so
 *	 we try to access any page that would cause a page fault early.
 */

	.section		.WindowVectors.text, "ax"


/* 4-Register Window Overflow Vector (Handler) */

	.align 64
.global _WindowOverflow4
_WindowOverflow4:
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5,  -8
	s32e	a3, a5,  -4
	rfwo


/* 4-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow4
_WindowUnderflow4:
	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5,  -8
	l32e	a3, a5,  -4
	rfwu


/* 8-Register Window Overflow Vector (Handler) */

	.align 64
.global _WindowOverflow8
_WindowOverflow8:
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

/* 8-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow8
_WindowUnderflow8:
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


/* 12-Register Window Overflow Vector (Handler) */

	.align 64
.global _WindowOverflow12
_WindowOverflow12:
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

/* 12-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow12
_WindowUnderflow12:
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

	.text


