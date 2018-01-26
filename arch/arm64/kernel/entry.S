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
#include <asm/mmu.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>
#include <asm/asm-uaccess.h>
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

	.macro kernel_ventry, el, label, regsize = 64
	.align 7
#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
alternative_if ARM64_UNMAP_KERNEL_AT_EL0
	.if	\el == 0
	.if	\regsize == 64
	mrs	x30, tpidrro_el0
	msr	tpidrro_el0, xzr
	.else
	mov	x30, xzr
	.endif
	.endif
alternative_else_nop_endif
#endif

	sub	sp, sp, #S_FRAME_SIZE
	b	el\()\el\()_\label
	.endm

	.macro tramp_alias, dst, sym
	mov_q	\dst, TRAMP_VALIAS
	add	\dst, \dst, #(\sym - .entry.tramp.text)
	.endm

	.macro	kernel_entry, el, regsize = 64
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
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr_this_cpu	tsk, __entry_task, x20	// Ensure MDSCR_EL1.SS is clear,
	ldr	x19, [tsk, #TSK_TI_FLAGS]	// since we can unmask debug
#else
	mov	tsk, sp
	and	tsk, tsk, #~(THREAD_SIZE - 1)	// Ensure MDSCR_EL1.SS is clear,
	ldr	x19, [tsk, #TI_FLAGS]		// since we can unmask debug
#endif
	disable_step_tsk x19, x20		// exceptions when scheduling.

	mov	x29, xzr			// fp pointed to user-space
	.else
	add	x21, sp, #S_FRAME_SIZE
	get_thread_info tsk
	/* Save the task's original addr_limit and set USER_DS (TASK_SIZE_64) */
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x20, [tsk, #TSK_TI_ADDR_LIMIT]
#else
	ldr	x20, [tsk, #TI_ADDR_LIMIT]
#endif
	str	x20, [sp, #S_ORIG_ADDR_LIMIT]
	mov	x20, #TASK_SIZE_64
#ifdef CONFIG_THREAD_INFO_IN_TASK
	str	x20, [tsk, #TSK_TI_ADDR_LIMIT]
#else
	str	x20, [tsk, #TI_ADDR_LIMIT]
#endif
	.endif /* \el == 0 */
	mrs	x22, elr_el1
	mrs	x23, spsr_el1
	stp	lr, x21, [sp, #S_LR]

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	/*
	 * Set the TTBR0 PAN bit in SPSR. When the exception is taken from
	 * EL0, there is no need to check the state of TTBR0_EL1 since
	 * accesses are always enabled.
	 * Note that the meaning of this bit differs from the ARMv8.1 PAN
	 * feature as all TTBR0_EL1 accesses are disabled, not just those to
	 * user mappings.
	 */
alternative_if ARM64_HAS_PAN
	b	1f				// skip TTBR0 PAN
alternative_else_nop_endif

	.if	\el != 0
	mrs	x21, ttbr0_el1
	tst	x21, #TTBR_ASID_MASK		// Check for the reserved ASID
	orr	x23, x23, #PSR_PAN_BIT		// Set the emulated PAN in the saved SPSR
	b.eq	1f				// TTBR0 access already disabled
	and	x23, x23, #~PSR_PAN_BIT		// Clear the emulated PAN in the saved SPSR
	.endif

	__uaccess_ttbr0_disable x21
1:
#endif

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
#ifdef CONFIG_THREAD_INFO_IN_TASK
	str	x20, [tsk, #TSK_TI_ADDR_LIMIT]
#else
	str	x20, [tsk, #TI_ADDR_LIMIT]
#endif
	.endif

	ldp	x21, x22, [sp, #S_PC]		// load ELR, SPSR
	.if	\el == 0
	ct_user_enter
	.endif

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
	/*
	 * Restore access to TTBR0_EL1. If returning to EL0, no need for SPSR
	 * PAN bit checking.
	 */
alternative_if ARM64_HAS_PAN
	b	2f				// skip TTBR0 PAN
alternative_else_nop_endif

	.if	\el != 0
	tbnz	x22, #22, 1f			// Skip re-enabling TTBR0 access if the PSR_PAN_BIT is set
	.endif

	__uaccess_ttbr0_enable x0, x1

	.if	\el == 0
	/*
	 * Enable errata workarounds only if returning to user. The only
	 * workaround currently required for TTBR0_EL1 changes are for the
	 * Cavium erratum 27456 (broadcast TLBI instructions may cause I-cache
	 * corruption).
	 */
	bl	post_ttbr_update_workaround
	.endif
1:
	.if	\el != 0
	and	x22, x22, #~PSR_PAN_BIT		// ARMv8.0 CPUs do not understand this bit
	.endif
2:
#endif

	.if	\el == 0
	ldr	x23, [sp, #S_SP]		// load return stack pointer
	msr	sp_el0, x23
	tst	x22, #PSR_MODE32_BIT		// native task?
	b.eq	3f

#ifdef CONFIG_ARM64_ERRATUM_845719
alternative_if ARM64_WORKAROUND_845719
#ifdef CONFIG_PID_IN_CONTEXTIDR
	mrs	x29, contextidr_el1
	msr	contextidr_el1, x29
#else
	msr contextidr_el1, xzr
#endif
alternative_else_nop_endif
#endif
3:
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

	.if	\el == 0
alternative_insn eret, nop, ARM64_UNMAP_KERNEL_AT_EL0
#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	bne	4f
	msr	far_el1, x30
	tramp_alias	x30, tramp_exit_native
	br	x30
4:
	tramp_alias	x30, tramp_exit_compat
	br	x30
#endif
	.else
	eret
	.endif
	.endm

	.macro	irq_stack_entry
	mov	x19, sp			// preserve the original sp

	/*
	 * Compare sp with the base of the task stack.
	 * If the top ~(THREAD_SIZE - 1) bits match, we are on a task stack,
	 * and should switch to the irq stack.
	 */
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x25, [tsk, TSK_STACK]
	eor	x25, x25, x19
	and	x25, x25, #~(THREAD_SIZE - 1)
	cbnz	x25, 9998f
#else
	and	x25, x19, #~(THREAD_SIZE - 1)
	cmp	x25, tsk
	b.ne	9998f
#endif

	adr_this_cpu x25, irq_stack, x26
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
	.pushsection ".entry.text", "ax"

	.align	11
ENTRY(vectors)
	kernel_ventry	1, sync_invalid			// Synchronous EL1t
	kernel_ventry	1, irq_invalid			// IRQ EL1t
	kernel_ventry	1, fiq_invalid			// FIQ EL1t
	kernel_ventry	1, error_invalid		// Error EL1t

	kernel_ventry	1, sync				// Synchronous EL1h
	kernel_ventry	1, irq				// IRQ EL1h
	kernel_ventry	1, fiq_invalid			// FIQ EL1h
	kernel_ventry	1, error_invalid		// Error EL1h

	kernel_ventry	0, sync				// Synchronous 64-bit EL0
	kernel_ventry	0, irq				// IRQ 64-bit EL0
	kernel_ventry	0, fiq_invalid			// FIQ 64-bit EL0
	kernel_ventry	0, error_invalid		// Error 64-bit EL0

#ifdef CONFIG_COMPAT
	kernel_ventry	0, sync_compat, 32		// Synchronous 32-bit EL0
	kernel_ventry	0, irq_compat, 32		// IRQ 32-bit EL0
	kernel_ventry	0, fiq_invalid_compat, 32	// FIQ 32-bit EL0
	kernel_ventry	0, error_invalid_compat, 32	// Error 32-bit EL0
#else
	kernel_ventry	0, sync_invalid, 32		// Synchronous 32-bit EL0
	kernel_ventry	0, irq_invalid, 32		// IRQ 32-bit EL0
	kernel_ventry	0, fiq_invalid, 32		// FIQ 32-bit EL0
	kernel_ventry	0, error_invalid, 32		// Error 32-bit EL0
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
	cmp	x24, #ESR_ELx_EC_IABT_CUR	// instruction abort in EL1
	b.eq	el1_ia
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

el1_ia:
	/*
	 * Fall through to the Data abort case
	 */
el1_da:
	/*
	 * Data abort handling
	 */
	mrs	x3, far_el1
	enable_dbg
	// re-enable interrupts if they were enabled in the aborted context
	tbnz	x23, #7, 1f			// PSR_I_BIT
	enable_irq
1:
	clear_address_tag x0, x3
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

	get_thread_info tsk
	irq_handler

#ifdef CONFIG_PREEMPT
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	w24, [tsk, #TSK_TI_PREEMPT]	// get preempt count
#else
	ldr	w24, [tsk, #TI_PREEMPT]		// get preempt count
#endif
	cbnz	w24, 1f				// preempt count != 0
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x0, [tsk, #TSK_TI_FLAGS]	// get flags
#else
	ldr	x0, [tsk, #TI_FLAGS]		// get flags
#endif
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
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x0, [tsk, #TSK_TI_FLAGS]	// get new tasks TI_FLAGS
#else
	ldr	x0, [tsk, #TI_FLAGS]		// get new tasks TI_FLAGS
#endif
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
	b.eq	el0_sys
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
	clear_address_tag x0, x26
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
	mov	x1, x25
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
el0_sys:
	/*
	 * System instructions, for trapped cache maintenance instructions
	 */
	enable_dbg_and_irq
	ct_user_exit
	mov	x0, x25
	mov	x1, sp
	bl	do_sysinstr
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
	bl	bad_el0_sync
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
#ifdef CONFIG_THREAD_INFO_IN_TASK
	msr	sp_el0, x1
#else
	and	x9, x9, #~(THREAD_SIZE - 1)
	msr	sp_el0, x9
#endif
	ret
ENDPROC(cpu_switch_to)

/*
 * This is the fast syscall return path.  We do as little as possible here,
 * and this includes saving x0 back into the kernel stack.
 */
ret_fast_syscall:
	disable_irq				// disable interrupts
	str	x0, [sp, #S_X0]			// returned x0
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x1, [tsk, #TSK_TI_FLAGS]	// re-check for syscall tracing
#else
	ldr	x1, [tsk, #TI_FLAGS]		// re-check for syscall tracing
#endif
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
#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x1, [tsk, #TSK_TI_FLAGS]
#else
	ldr	x1, [tsk, #TI_FLAGS]
#endif
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

#ifdef CONFIG_THREAD_INFO_IN_TASK
	ldr	x16, [tsk, #TSK_TI_FLAGS]	// check for syscall hooks
#else
	ldr	x16, [tsk, #TI_FLAGS]		// check for syscall hooks
#endif
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

	.popsection				// .entry.text

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
/*
 * Exception vectors trampoline.
 */
	.pushsection ".entry.tramp.text", "ax"

	.macro tramp_map_kernel, tmp
	mrs	\tmp, ttbr1_el1
	sub	\tmp, \tmp, #(SWAPPER_DIR_SIZE + RESERVED_TTBR0_SIZE)
	bic	\tmp, \tmp, #USER_ASID_FLAG
	msr	ttbr1_el1, \tmp
#ifdef CONFIG_ARCH_MSM8996
	/* ASID already in \tmp[63:48] */
	movk	\tmp, #:abs_g2_nc:(TRAMP_VALIAS >> 12)
	movk	\tmp, #:abs_g1_nc:(TRAMP_VALIAS >> 12)
	/* 2MB boundary containing the vectors, so we nobble the walk cache */
	movk	\tmp, #:abs_g0_nc:((TRAMP_VALIAS & ~(SZ_2M - 1)) >> 12)
	isb
	tlbi	vae1, \tmp
	dsb	nsh
#endif /* CONFIG_ARCH_MSM8996 */
	.endm

	.macro tramp_unmap_kernel, tmp
	mrs	\tmp, ttbr1_el1
	add	\tmp, \tmp, #(SWAPPER_DIR_SIZE + RESERVED_TTBR0_SIZE)
	orr	\tmp, \tmp, #USER_ASID_FLAG
	msr	ttbr1_el1, \tmp
	/*
	 * We avoid running the post_ttbr_update_workaround here because the
	 * user and kernel ASIDs don't have conflicting mappings, so any
	 * "blessing" as described in:
	 *
	 *   http://lkml.kernel.org/r/56BB848A.6060603@caviumnetworks.com
	 *
	 * will not hurt correctness. Whilst this may partially defeat the
	 * point of using split ASIDs in the first place, it avoids
	 * the hit of invalidating the entire I-cache on every return to
	 * userspace.
	 */
	.endm

	.macro tramp_ventry, regsize = 64
	.align	7
1:
	.if	\regsize == 64
	msr	tpidrro_el0, x30	// Restored in kernel_ventry
	.endif
	bl	2f
	b	.
2:
	tramp_map_kernel	x30
#ifdef CONFIG_RANDOMIZE_BASE
	adr	x30, tramp_vectors + PAGE_SIZE
#ifndef CONFIG_ARCH_MSM8996
	isb
#endif
	ldr	x30, [x30]
#else
	ldr	x30, =vectors
#endif
	prfm	plil1strm, [x30, #(1b - tramp_vectors)]
	msr	vbar_el1, x30
	add	x30, x30, #(1b - tramp_vectors)
	isb
	ret
	.endm

	.macro tramp_exit, regsize = 64
	adr	x30, tramp_vectors
	msr	vbar_el1, x30
	tramp_unmap_kernel	x30
	.if	\regsize == 64
	mrs	x30, far_el1
	.endif
	eret
	.endm

	.align	11
ENTRY(tramp_vectors)
	.space	0x400

	tramp_ventry
	tramp_ventry
	tramp_ventry
	tramp_ventry

	tramp_ventry	32
	tramp_ventry	32
	tramp_ventry	32
	tramp_ventry	32
END(tramp_vectors)

ENTRY(tramp_exit_native)
	tramp_exit
END(tramp_exit_native)

ENTRY(tramp_exit_compat)
	tramp_exit	32
END(tramp_exit_compat)

	.ltorg
	.popsection				// .entry.tramp.text
#ifdef CONFIG_RANDOMIZE_BASE
	.pushsection ".rodata", "a"
	.align PAGE_SHIFT
	.globl	__entry_tramp_data_start
__entry_tramp_data_start:
	.quad	vectors
	.popsection				// .rodata
#endif /* CONFIG_RANDOMIZE_BASE */
#endif /* CONFIG_UNMAP_KERNEL_AT_EL0 */

/*
 * Special system call wrappers.
 */
ENTRY(sys_rt_sigreturn_wrapper)
	mov	x0, sp
	b	sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn_wrapper)
