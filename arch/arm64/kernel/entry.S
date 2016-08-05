/*
 * Low-level exception handling code
 *
 * Copyright (C) 2012 ARM Ltd.
 * Authors:	Catalin Marinas <catalin.marinas@arm.com>
 *		Will Deacon <will.deacon@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/cpufeature.h>
#include <asm/errno.h>
#include <asm/esr.h>
#include <asm/irq.h>
#include <asm/memory.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

/*
 * Context tracking subsystem.  Used to instrument transitions
 * between user and kernel mode.
 */
	.macro ct_user_exit, syscall = 0
#ifdef CONFIG_CONTEXT_TRACKING
	bl	context_tracking_user_exit
	.if \syscall == 1
	/*
	 * Save/restore needed during syscalls.  Restore syscall arguments from
	 * the values already saved on stack during kernel_entry.
	 */
	ldp	x0, x1, [sp]
	ldp	x2, x3, [sp, #S_X2]
	ldp	x4, x5, [sp, #S_X4]
	ldp	x6, x7, [sp, #S_X6]
	.endif
#endif
	.endm

	.macro ct_user_enter
#ifdef CONFIG_CONTEXT_TRACKING
	bl	context_tracking_user_enter
#endif
	.endm

/*
 * Bad Abort numbers
 *-----------------
 */
#define BAD_SYNC	0
#define BAD_IRQ		1
#define BAD_FIQ		2
#define BAD_ERROR	3

	.macro	kernel_entry, el, regsize = 64
	sub	sp, sp, #S_FRAME_SIZE
	.if	\regsize == 32
	mov	w0, w0				// zero upper 32 bits of x0
	.endif
	stp	x0, x1, [sp, #16 * 0]
	stp	x2, x3, [sp, #16 * 1]
	stp	x4, x5, [sp, #16 * 2]
	stp	x6, x7, [sp, #16 * 3]
	stp	x8, x9, [sp, #16 * 4]
	stp	x10, x11, [sp, #16 * 5]
	stp	x12, x13, [sp, #16 * 6]
	stp	x14, x15, [sp, #16 * 7]
	stp	x16, x17, [sp, #16 * 8]
	stp	x18, x19, [sp, #16 * 9]
	stp	x20, x21, [sp, #16 * 10]
	stp	x22, x23, [sp, #16 * 11]
	stp	x24, x25, [sp, #16 * 12]
	stp	x26, x27, [sp, #16 * 13]
	stp	x28, x29, [sp, #16 * 14]

	.if	\el == 0
	mrs	x21, sp_el0
	mov	tsk, sp
	and	tsk, tsk, #~(THREAD_SIZE - 1)	// Ensure MDSCR_EL1.SS is clear,
	ldr	x19, [tsk, #TI_FLAGS]		// since we can unmask debug
	disable_step_tsk x19, x20		// exceptions when scheduling.

	mov	x29, xzr			// fp pointed to user-space
	.else
	add	x21, sp, #S_FRAME_SIZE
	get_thread_info tsk
	/* Save the task's original addr_limit and set USER_DS (TASK_SIZE_64) */
	ldr	x20, [tsk, #TI_ADDR_LIMIT]
	str	x20, [sp, #S_ORIG_ADDR_LIMIT]
	mov	x20, #TASK_SIZE_64
	str	x20, [tsk, #TI_ADDR_LIMIT]
	ALTERNATIVE(nop, SET_PSTATE_UAO(0), ARM64_HAS_UAO, CONFIG_ARM64_UAO)
	.endif /* \el == 0 */
	mrs	x22, elr_el1
	mrs	x23, spsr_el1
	stp	lr, x21, [sp, #S_LR]
	stp	x22, x23, [sp, #S_PC]

	/*
	 * Set syscallno to -1 by default (overridden later if real syscall).
	 */
	.if	\el == 0
	mvn	x21, xzr
	str	x21, [sp, #S_SYSCALLNO]
	.endif

	/*
	 * Set sp_el0 to current thread_info.
	 */
	.if	\el == 0
	msr	sp_el0, tsk
	.endif

	/*
	 * Registers that may be useful after this macro is invoked:
	 *
	 * x21 - aborted SP
	 * x22 - aborted PC
	 * x23 - aborted PSTATE
	*/
	.endm

	.macro	kernel_exit, el
	.if	\el != 0
	/* Restore the task's original addr_limit. */
	ldr	x20, [sp, #S_ORIG_ADDR_LIMIT]
	str	x20, [tsk, #TI_ADDR_LIMIT]

	/* No need to restore UAO, it will be restored from SPSR_EL1 */
	.endif

	ldp	x21, x22, [sp, #S_PC]		// load ELR, SPSR
	.if	\el == 0
	ct_user_enter
	ldr	x23, [sp, #S_SP]		// load return stack pointer
	msr	sp_el0, x23
#ifdef CONFIG_ARM64_ERRATUM_845719
alternative_if_not ARM64_WORKAROUND_845719
	nop
	nop
#ifdef CONFIG_PID_IN_CONTEXTIDR
	nop
#endif
alternative_else
	tbz	x22, #4, 1f
#ifdef CONFIG_PID_IN_CONTEXTIDR
	mrs	x29, contextidr_el1
	msr	contextidr_el1, x29
#else
	msr contextidr_el1, xzr
#endif
1:
alternative_endif
#endif
	.endif
	msr	elr_el1, x21			// set up the return data
	msr	spsr_el1, x22
	ldp	x0, x1, [sp, #16 * 0]
	ldp	x2, x3, [sp, #16 * 1]
	ldp	x4, x5, [sp, #16 * 2]
	ldp	x6, x7, [sp, #16 * 3]
	ldp	x8, x9, [sp, #16 * 4]
	ldp	x10, x11, [sp, #16 * 5]
	ldp	x12, x13, [sp, #16 * 6]
	ldp	x14, x15, [sp, #16 * 7]
	ldp	x16, x17, [sp, #16 * 8]
	ldp	x18, x19, [sp, #16 * 9]
	ldp	x20, x21, [sp, #16 * 10]
	ldp	x22, x23, [sp, #16 * 11]
	ldp	x24, x25, [sp, #16 * 12]
	ldp	x26, x27, [sp, #16 * 13]
	ldp	x28, x29, [sp, #16 * 14]
	ldr	lr, [sp, #S_LR]
	add	sp, sp, #S_FRAME_SIZE		// restore sp
	eret					// return to kernel
	.endm

	.macro	get_thread_info, rd
	mrs	\rd, sp_el0
	.endm

	.macro	irq_stack_entry
	mov	x19, sp			// preserve the original sp

	/*
	 * Compare sp with the current thread_info, if the top
	 * ~(THREAD_SIZE - 1) bits match, we are on a task stack, and
	 * should switch to the irq stack.
	 */
	and	x25, x19, #~(THREAD_SIZE - 1)
	cmp	x25, tsk
	b.ne	9998f

	this_cpu_ptr irq_stack, x25, x26
	mov	x26, #IRQ_STACK_START_SP
	add	x26, x25, x26

	/* switch to the irq stack */
	mov	sp, x26

	/*
	 * Add a dummy stack frame, this non-standard format is fixed up
	 * by unwind_frame()
	 */
	stp     x29, x19, [sp, #-16]!
	mov	x29, sp

9998:
	.endm

	/*
	 * x19 should be preserved between irq_stack_entry and
	 * irq_stack_exit.
	 */
	.macro	irq_stack_exit
	mov	sp, x19
	.endm

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function - x0 to x6.
 *
 * x7 is reserved for the system call number in 32-bit mode.
 */
sc_nr	.req	x25		// number of system calls
scno	.req	x26		// syscall number
stbl	.req	x27		// syscall table pointer
tsk	.req	x28		// current thread_info

/*
 * Interrupt handling.
 */
	.macro	irq_handler
	ldr_l	x1, handle_arch_irq
	mov	x0, sp
	irq_stack_entry
	blr	x1
	irq_stack_exit
	.endm

	.text

/*
 * Exception vectors.
 */

	.align	11
ENTRY(vectors)
	ventry	el1_sync_invalid		// Synchronous EL1t
	ventry	el1_irq_invalid			// IRQ EL1t
	ventry	el1_fiq_invalid			// FIQ EL1t
	ventry	el1_error_invalid		// Error EL1t

	ventry	el1_sync			// Synchronous EL1h
	ventry	el1_irq				// IRQ EL1h
	ventry	el1_fiq_invalid			// FIQ EL1h
	ventry	el1_error_invalid		// Error EL1h

	ventry	el0_sync			// Synchronous 64-bit EL0
	ventry	el0_irq				// IRQ 64-bit EL0
	ventry	el0_fiq_invalid			// FIQ 64-bit EL0
	ventry	el0_error_invalid		// Error 64-bit EL0

#ifdef CONFIG_COMPAT
	ventry	el0_sync_compat			// Synchronous 32-bit EL0
	ventry	el0_irq_compat			// IRQ 32-bit EL0
	ventry	el0_fiq_invalid_compat		// FIQ 32-bit EL0
	ventry	el0_error_invalid_compat	// Error 32-bit EL0
#else
	ventry	el0_sync_invalid		// Synchronous 32-bit EL0
	ventry	el0_irq_invalid			// IRQ 32-bit EL0
	ventry	el0_fiq_invalid			// FIQ 32-bit EL0
	ventry	el0_error_invalid		// Error 32-bit EL0
#endif
END(vectors)

/*
 * Invalid mode handlers
 */
	.macro	inv_entry, el, reason, regsize = 64
	kernel_entry \el, \regsize
	mov	x0, sp
	mov	x1, #\reason
	mrs	x2, esr_el1
	b	bad_mode
	.endm

el0_sync_invalid:
	inv_entry 0, BAD_SYNC
ENDPROC(el0_sync_invalid)

el0_irq_invalid:
	inv_entry 0, BAD_IRQ
ENDPROC(el0_irq_invalid)

el0_fiq_invalid:
	inv_entry 0, BAD_FIQ
ENDPROC(el0_fiq_invalid)

el0_error_invalid:
	inv_entry 0, BAD_ERROR
ENDPROC(el0_error_invalid)

#ifdef CONFIG_COMPAT
el0_fiq_invalid_compat:
	inv_entry 0, BAD_FIQ, 32
ENDPROC(el0_fiq_invalid_compat)

el0_error_invalid_compat:
	inv_entry 0, BAD_ERROR, 32
ENDPROC(el0_error_invalid_compat)
#endif

el1_sync_invalid:
	inv_entry 1, BAD_SYNC
ENDPROC(el1_sync_invalid)

el1_irq_invalid:
	inv_entry 1, BAD_IRQ
ENDPROC(el1_irq_invalid)

el1_fiq_invalid:
	inv_entry 1, BAD_FIQ
ENDPROC(el1_fiq_invalid)

el1_error_invalid:
	inv_entry 1, BAD_ERROR
ENDPROC(el1_error_invalid)

/*
 * EL1 mode handlers.
 */
	.align	6
el1_sync:
	kernel_entry 1
	mrs	x1, esr_el1			// read the syndrome register
	lsr	x24, x1, #ESR_ELx_EC_SHIFT	// exception class
	cmp	x24, #ESR_ELx_EC_DABT_CUR	// data abort in EL1
	b.eq	el1_da
	cmp	x24, #ESR_ELx_EC_SYS64		// configurable trap
	b.eq	el1_undef
	cmp	x24, #ESR_ELx_EC_SP_ALIGN	// stack alignment exception
	b.eq	el1_sp_pc
	cmp	x24, #ESR_ELx_EC_PC_ALIGN	// pc alignment exception
	b.eq	el1_sp_pc
	cmp	x24, #ESR_ELx_EC_UNKNOWN	// unknown exception in EL1
	b.eq	el1_undef
	cmp	x24, #ESR_ELx_EC_BREAKPT_CUR	// debug exception in EL1
	b.ge	el1_dbg
	b	el1_inv
el1_da:
	/*
	 * Data abort handling
	 */
	mrs	x0, far_el1
	enable_dbg
	// re-enable interrupts if they were enabled in the aborted context
	tbnz	x23, #7, 1f			// PSR_I_BIT
	enable_irq
1:
	mov	x2, sp				// struct pt_regs
	bl	do_mem_abort

	// disable interrupts before pulling preserved data off the stack
	disable_irq
	kernel_exit 1
el1_sp_pc:
	/*
	 * Stack or PC alignment exception handling
	 */
	mrs	x0, far_el1
	enable_dbg
	mov	x2, sp
	b	do_sp_pc_abort
el1_undef:
	/*
	 * Undefined instruction
	 */
	enable_dbg
	mov	x0, sp
	b	do_undefinstr
el1_dbg:
	/*
	 * Debug exception handling
	 */
	cmp	x24, #ESR_ELx_EC_BRK64		// if BRK64
	cinc	x24, x24, eq			// set bit '0'
	tbz	x24, #0, el1_inv		// EL1 only
	mrs	x0, far_el1
	mov	x2, sp				// struct pt_regs
	bl	do_debug_exception
	kernel_exit 1
el1_inv:
	// TODO: add support for undefined instructions in kernel mode
	enable_dbg
	mov	x0, sp
	mov	x2, x1
	mov	x1, #BAD_SYNC
	b	bad_mode
ENDPROC(el1_sync)

	.align	6
el1_irq:
	kernel_entry 1
	enable_dbg
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

	irq_handler

#ifdef CONFIG_PREEMPT
	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
	cbnz	w24, 1f				// preempt count != 0
	ldr	x0, [tsk, #TI_FLAGS]		// get flags
	tbz	x0, #TIF_NEED_RESCHED, 1f	// needs rescheduling?
	bl	el1_preempt
1:
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	kernel_exit 1
ENDPROC(el1_irq)

#ifdef CONFIG_PREEMPT
el1_preempt:
	mov	x24, lr
1:	bl	preempt_schedule_irq		// irq en/disable is done inside
	ldr	x0, [tsk, #TI_FLAGS]		// get new tasks TI_FLAGS
	tbnz	x0, #TIF_NEED_RESCHED, 1b	// needs rescheduling?
	ret	x24
#endif

/*
 * EL0 mode handlers.
 */
	.align	6
el0_sync:
	kernel_entry 0
	mrs	x25, esr_el1			// read the syndrome register
	lsr	x24, x25, #ESR_ELx_EC_SHIFT	// exception class
	cmp	x24, #ESR_ELx_EC_SVC64		// SVC in 64-bit state
	b.eq	el0_svc
	cmp	x24, #ESR_ELx_EC_DABT_LOW	// data abort in EL0
	b.eq	el0_da
	cmp	x24, #ESR_ELx_EC_IABT_LOW	// instruction abort in EL0
	b.eq	el0_ia
	cmp	x24, #ESR_ELx_EC_FP_ASIMD	// FP/ASIMD access
	b.eq	el0_fpsimd_acc
	cmp	x24, #ESR_ELx_EC_FP_EXC64	// FP/ASIMD exception
	b.eq	el0_fpsimd_exc
	cmp	x24, #ESR_ELx_EC_SYS64		// configurable trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_SP_ALIGN	// stack alignment exception
	b.eq	el0_sp_pc
	cmp	x24, #ESR_ELx_EC_PC_ALIGN	// pc alignment exception
	b.eq	el0_sp_pc
	cmp	x24, #ESR_ELx_EC_UNKNOWN	// unknown exception in EL0
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_BREAKPT_LOW	// debug exception in EL0
	b.ge	el0_dbg
	b	el0_inv

#ifdef CONFIG_COMPAT
	.align	6
el0_sync_compat:
	kernel_entry 0, 32
	mrs	x25, esr_el1			// read the syndrome register
	lsr	x24, x25, #ESR_ELx_EC_SHIFT	// exception class
	cmp	x24, #ESR_ELx_EC_SVC32		// SVC in 32-bit state
	b.eq	el0_svc_compat
	cmp	x24, #ESR_ELx_EC_DABT_LOW	// data abort in EL0
	b.eq	el0_da
	cmp	x24, #ESR_ELx_EC_IABT_LOW	// instruction abort in EL0
	b.eq	el0_ia
	cmp	x24, #ESR_ELx_EC_FP_ASIMD	// FP/ASIMD access
	b.eq	el0_fpsimd_acc
	cmp	x24, #ESR_ELx_EC_FP_EXC32	// FP/ASIMD exception
	b.eq	el0_fpsimd_exc
	cmp	x24, #ESR_ELx_EC_PC_ALIGN	// pc alignment exception
	b.eq	el0_sp_pc
	cmp	x24, #ESR_ELx_EC_UNKNOWN	// unknown exception in EL0
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP15_32	// CP15 MRC/MCR trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP15_64	// CP15 MRRC/MCRR trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP14_MR	// CP14 MRC/MCR trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP14_LS	// CP14 LDC/STC trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP14_64	// CP14 MRRC/MCRR trap
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_BREAKPT_LOW	// debug exception in EL0
	b.ge	el0_dbg
	b	el0_inv
el0_svc_compat:
	/*
	 * AArch32 syscall handling
	 */
	adrp	stbl, compat_sys_call_table	// load compat syscall table pointer
	uxtw	scno, w7			// syscall number in w7 (r7)
	mov     sc_nr, #__NR_compat_syscalls
	b	el0_svc_naked

	.align	6
el0_irq_compat:
	kernel_entry 0, 32
	b	el0_irq_naked
#endif

el0_da:
	/*
	 * Data abort handling
	 */
	mrs	x26, far_el1
	// enable interrupts before calling the main handler
	enable_dbg_and_irq
	ct_user_exit
	bic	x0, x26, #(0xff << 56)
	mov	x1, x25
	mov	x2, sp
	bl	do_mem_abort
	b	ret_to_user
el0_ia:
	/*
	 * Instruction abort handling
	 */
	mrs	x26, far_el1
	// enable interrupts before calling the main handler
	enable_dbg_and_irq
	ct_user_exit
	mov	x0, x26
	orr	x1, x25, #1 << 24		// use reserved ISS bit for instruction aborts
	mov	x2, sp
	bl	do_mem_abort
	b	ret_to_user
el0_fpsimd_acc:
	/*
	 * Floating Point or Advanced SIMD access
	 */
	enable_dbg
	ct_user_exit
	mov	x0, x25
	mov	x1, sp
	bl	do_fpsimd_acc
	b	ret_to_user
el0_fpsimd_exc:
	/*
	 * Floating Point or Advanced SIMD exception
	 */
	enable_dbg
	ct_user_exit
	mov	x0, x25
	mov	x1, sp
	bl	do_fpsimd_exc
	b	ret_to_user
el0_sp_pc:
	/*
	 * Stack or PC alignment exception handling
	 */
	mrs	x26, far_el1
	// enable interrupts before calling the main handler
	enable_dbg_and_irq
	ct_user_exit
	mov	x0, x26
	mov	x1, x25
	mov	x2, sp
	bl	do_sp_pc_abort
	b	ret_to_user
el0_undef:
	/*
	 * Undefined instruction
	 */
	// enable interrupts before calling the main handler
	enable_dbg_and_irq
	ct_user_exit
	mov	x0, sp
	bl	do_undefinstr
	b	ret_to_user
el0_dbg:
	/*
	 * Debug exception handling
	 */
	tbnz	x24, #0, el0_inv		// EL0 only
	mrs	x0, far_el1
	mov	x1, x25
	mov	x2, sp
	bl	do_debug_exception
	enable_dbg
	ct_user_exit
	b	ret_to_user
el0_inv:
	enable_dbg
	ct_user_exit
	mov	x0, sp
	mov	x1, #BAD_SYNC
	mov	x2, x25
	bl	bad_mode
	b	ret_to_user
ENDPROC(el0_sync)

	.align	6
el0_irq:
	kernel_entry 0
el0_irq_naked:
	enable_dbg
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

	ct_user_exit
	irq_handler

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	b	ret_to_user
ENDPROC(el0_irq)

/*
 * Register switch for AArch64. The callee-saved registers need to be saved
 * and restored. On entry:
 *   x0 = previous task_struct (must be preserved across the switch)
 *   x1 = next task_struct
 * Previous and next are guaranteed not to be the same.
 *
 */
ENTRY(cpu_switch_to)
	mov	x10, #THREAD_CPU_CONTEXT
	add	x8, x0, x10
	mov	x9, sp
	stp	x19, x20, [x8], #16		// store callee-saved registers
	stp	x21, x22, [x8], #16
	stp	x23, x24, [x8], #16
	stp	x25, x26, [x8], #16
	stp	x27, x28, [x8], #16
	stp	x29, x9, [x8], #16
	str	lr, [x8]
	add	x8, x1, x10
	ldp	x19, x20, [x8], #16		// restore callee-saved registers
	ldp	x21, x22, [x8], #16
	ldp	x23, x24, [x8], #16
	ldp	x25, x26, [x8], #16
	ldp	x27, x28, [x8], #16
	ldp	x29, x9, [x8], #16
	ldr	lr, [x8]
	mov	sp, x9
	and	x9, x9, #~(THREAD_SIZE - 1)
	msr	sp_el0, x9
	ret
ENDPROC(cpu_switch_to)

/*
 * This is the fast syscall return path.  We do as little as possible here,
 * and this includes saving x0 back into the kernel stack.
 */
ret_fast_syscall:
	disable_irq				// disable interrupts
	str	x0, [sp, #S_X0]			// returned x0
	ldr	x1, [tsk, #TI_FLAGS]		// re-check for syscall tracing
	and	x2, x1, #_TIF_SYSCALL_WORK
	cbnz	x2, ret_fast_syscall_trace
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending
	enable_step_tsk x1, x2
	kernel_exit 0
ret_fast_syscall_trace:
	enable_irq				// enable interrupts
	b	__sys_trace_return_skipped	// we already saved x0

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
work_pending:
	tbnz	x1, #TIF_NEED_RESCHED, work_resched
	/* TIF_SIGPENDING, TIF_NOTIFY_RESUME or TIF_FOREIGN_FPSTATE case */
	mov	x0, sp				// 'regs'
	enable_irq				// enable interrupts for do_notify_resume()
	bl	do_notify_resume
	b	ret_to_user
work_resched:
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off		// the IRQs are off here, inform the tracing code
#endif
	bl	schedule

/*
 * "slow" syscall return path.
 */
ret_to_user:
	disable_irq				// disable interrupts
	ldr	x1, [tsk, #TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending
	enable_step_tsk x1, x2
	kernel_exit 0
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread
	mov	x0, x20
	blr	x19
1:	get_thread_info tsk
	b	ret_to_user
ENDPROC(ret_from_fork)

/*
 * SVC handler.
 */
	.align	6
el0_svc:
	adrp	stbl, sys_call_table		// load syscall table pointer
	uxtw	scno, w8			// syscall number in w8
	mov	sc_nr, #__NR_syscalls
el0_svc_naked:					// compat entry point
	stp	x0, scno, [sp, #S_ORIG_X0]	// save the original x0 and syscall number
	enable_dbg_and_irq
	ct_user_exit 1

	ldr	x16, [tsk, #TI_FLAGS]		// check for syscall hooks
	tst	x16, #_TIF_SYSCALL_WORK
	b.ne	__sys_trace
	cmp     scno, sc_nr                     // check upper syscall limit
	b.hs	ni_sys
	ldr	x16, [stbl, scno, lsl #3]	// address in the syscall table
	blr	x16				// call sys_* routine
	b	ret_fast_syscall
ni_sys:
	mov	x0, sp
	bl	do_ni_syscall
	b	ret_fast_syscall
ENDPROC(el0_svc)

	/*
	 * This is the really slow path.  We're going to be doing context
	 * switches, and waiting for our parent to respond.
	 */
__sys_trace:
	mov	w0, #-1				// set default errno for
	cmp     scno, x0			// user-issued syscall(-1)
	b.ne	1f
	mov	x0, #-ENOSYS
	str	x0, [sp, #S_X0]
1:	mov	x0, sp
	bl	syscall_trace_enter
	cmp	w0, #-1				// skip the syscall?
	b.eq	__sys_trace_return_skipped
	uxtw	scno, w0			// syscall number (possibly new)
	mov	x1, sp				// pointer to regs
	cmp	scno, sc_nr			// check upper syscall limit
	b.hs	__ni_sys_trace
	ldp	x0, x1, [sp]			// restore the syscall args
	ldp	x2, x3, [sp, #S_X2]
	ldp	x4, x5, [sp, #S_X4]
	ldp	x6, x7, [sp, #S_X6]
	ldr	x16, [stbl, scno, lsl #3]	// address in the syscall table
	blr	x16				// call sys_* routine

__sys_trace_return:
	str	x0, [sp, #S_X0]			// save returned x0
__sys_trace_return_skipped:
	mov	x0, sp
	bl	syscall_trace_exit
	b	ret_to_user

__ni_sys_trace:
	mov	x0, sp
	bl	do_ni_syscall
	b	__sys_trace_return

/*
 * Special system call wrappers.
 */
ENTRY(sys_rt_sigreturn_wrapper)
	mov	x0, sp
	b	sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn_wrapper)
