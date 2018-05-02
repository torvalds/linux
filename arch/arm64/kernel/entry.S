/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Low-level exception handling code
 *
 * Copyright (C) 2012 ARM Ltd.
 * Authors:	Catalin Marinas <catalin.marinas@arm.com>
 *		Will Deacon <will.deacon@arm.com>
 */

#include <linux/arm-smccc.h>
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
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-uaccess.h>
#include <asm/unistd.h>

/*
 * Context tracking subsystem.  Used to instrument transitions
 * between user and kernel mode.
 */
	.macro ct_user_exit_irqoff
#ifdef CONFIG_CONTEXT_TRACKING
	bl	enter_from_user_mode
#endif
	.endm

	.macro ct_user_enter
#ifdef CONFIG_CONTEXT_TRACKING
	bl	context_tracking_user_enter
#endif
	.endm

	.macro	clear_gp_regs
	.irp	n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29
	mov	x\n, xzr
	.endr
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
#ifdef CONFIG_VMAP_STACK
	/*
	 * Test whether the SP has overflowed, without corrupting a GPR.
	 * Task and IRQ stacks are aligned to (1 << THREAD_SHIFT).
	 */
	add	sp, sp, x0			// sp' = sp + x0
	sub	x0, sp, x0			// x0' = sp' - x0 = (sp + x0) - x0 = sp
	tbnz	x0, #THREAD_SHIFT, 0f
	sub	x0, sp, x0			// x0'' = sp' - x0' = (sp + x0) - sp = x0
	sub	sp, sp, x0			// sp'' = sp' - x0 = (sp + x0) - x0 = sp
	b	el\()\el\()_\label

0:
	/*
	 * Either we've just detected an overflow, or we've taken an exception
	 * while on the overflow stack. Either way, we won't return to
	 * userspace, and can clobber EL0 registers to free up GPRs.
	 */

	/* Stash the original SP (minus S_FRAME_SIZE) in tpidr_el0. */
	msr	tpidr_el0, x0

	/* Recover the original x0 value and stash it in tpidrro_el0 */
	sub	x0, sp, x0
	msr	tpidrro_el0, x0

	/* Switch to the overflow stack */
	adr_this_cpu sp, overflow_stack + OVERFLOW_STACK_SIZE, x0

	/*
	 * Check whether we were already on the overflow stack. This may happen
	 * after panic() re-enables interrupts.
	 */
	mrs	x0, tpidr_el0			// sp of interrupted context
	sub	x0, sp, x0			// delta with top of overflow stack
	tst	x0, #~(OVERFLOW_STACK_SIZE - 1)	// within range?
	b.ne	__bad_stack			// no? -> bad stack pointer

	/* We were already on the overflow stack. Restore sp/x0 and carry on. */
	sub	sp, sp, x0
	mrs	x0, tpidrro_el0
#endif
	b	el\()\el\()_\label
	.endm

	.macro tramp_alias, dst, sym
	mov_q	\dst, TRAMP_VALIAS
	add	\dst, \dst, #(\sym - .entry.tramp.text)
	.endm

	// This macro corrupts x0-x3. It is the caller's duty
	// to save/restore them if required.
	.macro	apply_ssbd, state, tmp1, tmp2
#ifdef CONFIG_ARM64_SSBD
alternative_cb	arm64_enable_wa2_handling
	b	.L__asm_ssbd_skip\@
alternative_cb_end
	ldr_this_cpu	\tmp2, arm64_ssbd_callback_required, \tmp1
	cbz	\tmp2,	.L__asm_ssbd_skip\@
	ldr	\tmp2, [tsk, #TSK_TI_FLAGS]
	tbnz	\tmp2, #TIF_SSBD, .L__asm_ssbd_skip\@
	mov	w0, #ARM_SMCCC_ARCH_WORKAROUND_2
	mov	w1, #\state
alternative_cb	arm64_update_smccc_conduit
	nop					// Patched to SMC/HVC #0
alternative_cb_end
.L__asm_ssbd_skip\@:
#endif
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
	clear_gp_regs
	mrs	x21, sp_el0
	ldr_this_cpu	tsk, __entry_task, x20	// Ensure MDSCR_EL1.SS is clear,
	ldr	x19, [tsk, #TSK_TI_FLAGS]	// since we can unmask debug
	disable_step_tsk x19, x20		// exceptions when scheduling.

	apply_ssbd 1, x22, x23

#ifdef CONFIG_SHADOW_CALL_STACK
	ldr	x18, [tsk, #TSK_TI_SCS]		// Restore shadow call stack
	str	xzr, [tsk, #TSK_TI_SCS]		// Limit visibility of saved SCS
#endif
	.else
	add	x21, sp, #S_FRAME_SIZE
	get_current_task tsk
	/* Save the task's original addr_limit and set USER_DS */
	ldr	x20, [tsk, #TSK_TI_ADDR_LIMIT]
	str	x20, [sp, #S_ORIG_ADDR_LIMIT]
	mov	x20, #USER_DS
	str	x20, [tsk, #TSK_TI_ADDR_LIMIT]
	/* No need to reset PSTATE.UAO, hardware's already set it to 0 for us */
	.endif /* \el == 0 */
	mrs	x22, elr_el1
	mrs	x23, spsr_el1
	stp	lr, x21, [sp, #S_LR]

	/*
	 * In order to be able to dump the contents of struct pt_regs at the
	 * time the exception was taken (in case we attempt to walk the call
	 * stack later), chain it together with the stack frames.
	 */
	.if \el == 0
	stp	xzr, xzr, [sp, #S_STACKFRAME]
	.else
	stp	x29, x22, [sp, #S_STACKFRAME]
	.endif
	add	x29, sp, #S_STACKFRAME

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

	/* Not in a syscall by default (el0_svc overwrites for real syscall) */
	.if	\el == 0
	mov	w21, #NO_SYSCALL
	str	w21, [sp, #S_SYSCALLNO]
	.endif

	/*
	 * Set sp_el0 to current thread_info.
	 */
	.if	\el == 0
	msr	sp_el0, tsk
	.endif

	/* Save pmr */
alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	mrs_s	x20, SYS_ICC_PMR_EL1
	str	x20, [sp, #S_PMR_SAVE]
alternative_else_nop_endif

	/*
	 * Registers that may be useful after this macro is invoked:
	 *
	 * x20 - ICC_PMR_EL1
	 * x21 - aborted SP
	 * x22 - aborted PC
	 * x23 - aborted PSTATE
	*/
	.endm

	.macro	kernel_exit, el
	.if	\el != 0
	disable_daif

	/* Restore the task's original addr_limit. */
	ldr	x20, [sp, #S_ORIG_ADDR_LIMIT]
	str	x20, [tsk, #TSK_TI_ADDR_LIMIT]

	/* No need to restore UAO, it will be restored from SPSR_EL1 */
	.endif

	/* Restore pmr */
alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	ldr	x20, [sp, #S_PMR_SAVE]
	msr_s	SYS_ICC_PMR_EL1, x20
	/* Ensure priority change is seen by redistributor */
	dsb	sy
alternative_else_nop_endif

	ldp	x21, x22, [sp, #S_PC]		// load ELR, SPSR
	.if	\el == 0
	ct_user_enter
	.endif

#ifdef CONFIG_SHADOW_CALL_STACK
	.if	\el == 0
	str	x18, [tsk, #TSK_TI_SCS]		// Save shadow call stack
	.endif
#endif

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
#ifdef CONFIG_ARM64_ERRATUM_1418040
alternative_if_not ARM64_WORKAROUND_1418040
	b	4f
alternative_else_nop_endif
	/*
	 * if (x22.mode32 == cntkctl_el1.el0vcten)
	 *     cntkctl_el1.el0vcten = ~cntkctl_el1.el0vcten
	 */
	mrs	x1, cntkctl_el1
	eon	x0, x1, x22, lsr #3
	tbz	x0, #1, 4f
	eor	x1, x1, #2	// ARCH_TIMER_USR_VCT_ACCESS_EN
	msr	cntkctl_el1, x1
4:
#endif
	apply_ssbd 0, x0, x1
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
	bne	5f
	msr	far_el1, x30
	tramp_alias	x30, tramp_exit_native
	br	x30
5:
	tramp_alias	x30, tramp_exit_compat
	br	x30
#endif
	.else
	eret
	.endif
	sb
	.endm

	.macro	irq_stack_entry
	mov	x19, sp			// preserve the original sp
#ifdef CONFIG_SHADOW_CALL_STACK
	mov	x20, x18		// preserve the original shadow stack
#endif

	/*
	 * Compare sp with the base of the task stack.
	 * If the top ~(THREAD_SIZE - 1) bits match, we are on a task stack,
	 * and should switch to the irq stack.
	 */
	ldr	x25, [tsk, TSK_STACK]
	eor	x25, x25, x19
	and	x25, x25, #~(THREAD_SIZE - 1)
	cbnz	x25, 9998f

	ldr_this_cpu x25, irq_stack_ptr, x26
	mov	x26, #IRQ_STACK_SIZE
	add	x26, x25, x26

	/* switch to the irq stack */
	mov	sp, x26

#ifdef CONFIG_SHADOW_CALL_STACK
	/* also switch to the irq shadow stack */
	ldr_this_cpu x18, irq_shadow_call_stack_ptr, x26
#endif

9998:
	.endm

	/*
	 * The callee-saved regs (x19-x29) should be preserved between
	 * irq_stack_entry and irq_stack_exit.
	 */
	.macro	irq_stack_exit
	mov	sp, x19
#ifdef CONFIG_SHADOW_CALL_STACK
	mov	x18, x20
#endif
	.endm

/* GPRs used by entry code */
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

#ifdef CONFIG_ARM64_PSEUDO_NMI
	/*
	 * Set res to 0 if irqs were unmasked in interrupted context.
	 * Otherwise set res to non-0 value.
	 */
	.macro	test_irqs_unmasked res:req, pmr:req
alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	sub	\res, \pmr, #GIC_PRIO_IRQON
alternative_else
	mov	\res, xzr
alternative_endif
	.endm
#endif

	.macro	gic_prio_kentry_setup, tmp:req
#ifdef CONFIG_ARM64_PSEUDO_NMI
	alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	mov	\tmp, #(GIC_PRIO_PSR_I_SET | GIC_PRIO_IRQON)
	msr_s	SYS_ICC_PMR_EL1, \tmp
	alternative_else_nop_endif
#endif
	.endm

	.macro	gic_prio_irq_setup, pmr:req, tmp:req
#ifdef CONFIG_ARM64_PSEUDO_NMI
	alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	orr	\tmp, \pmr, #GIC_PRIO_PSR_I_SET
	msr_s	SYS_ICC_PMR_EL1, \tmp
	alternative_else_nop_endif
#endif
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
	kernel_ventry	1, error			// Error EL1h

	kernel_ventry	0, sync				// Synchronous 64-bit EL0
	kernel_ventry	0, irq				// IRQ 64-bit EL0
	kernel_ventry	0, fiq_invalid			// FIQ 64-bit EL0
	kernel_ventry	0, error			// Error 64-bit EL0

#ifdef CONFIG_COMPAT
	kernel_ventry	0, sync_compat, 32		// Synchronous 32-bit EL0
	kernel_ventry	0, irq_compat, 32		// IRQ 32-bit EL0
	kernel_ventry	0, fiq_invalid_compat, 32	// FIQ 32-bit EL0
	kernel_ventry	0, error_compat, 32		// Error 32-bit EL0
#else
	kernel_ventry	0, sync_invalid, 32		// Synchronous 32-bit EL0
	kernel_ventry	0, irq_invalid, 32		// IRQ 32-bit EL0
	kernel_ventry	0, fiq_invalid, 32		// FIQ 32-bit EL0
	kernel_ventry	0, error_invalid, 32		// Error 32-bit EL0
#endif
END(vectors)

#ifdef CONFIG_VMAP_STACK
	/*
	 * We detected an overflow in kernel_ventry, which switched to the
	 * overflow stack. Stash the exception regs, and head to our overflow
	 * handler.
	 */
__bad_stack:
	/* Restore the original x0 value */
	mrs	x0, tpidrro_el0

	/*
	 * Store the original GPRs to the new stack. The orginal SP (minus
	 * S_FRAME_SIZE) was stashed in tpidr_el0 by kernel_ventry.
	 */
	sub	sp, sp, #S_FRAME_SIZE
	kernel_entry 1
	mrs	x0, tpidr_el0
	add	x0, x0, #S_FRAME_SIZE
	str	x0, [sp, #S_SP]

	/* Stash the regs for handle_bad_stack */
	mov	x0, sp

	/* Time to die */
	bl	handle_bad_stack
	ASM_BUG()
#endif /* CONFIG_VMAP_STACK */

/*
 * Invalid mode handlers
 */
	.macro	inv_entry, el, reason, regsize = 64
	kernel_entry \el, \regsize
	mov	x0, sp
	mov	x1, #\reason
	mrs	x2, esr_el1
	bl	bad_mode
	ASM_BUG()
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
	cmp	x24, #ESR_ELx_EC_PC_ALIGN	// pc alignment exception
	b.eq	el1_pc
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
	inherit_daif	pstate=x23, tmp=x2
	untagged_addr	x0, x3
	mov	x2, sp				// struct pt_regs
	bl	do_mem_abort

	kernel_exit 1
el1_pc:
	/*
	 * PC alignment exception handling. We don't handle SP alignment faults,
	 * since we will have hit a recursive exception when trying to push the
	 * initial pt_regs.
	 */
	mrs	x0, far_el1
	inherit_daif	pstate=x23, tmp=x2
	mov	x2, sp
	bl	do_sp_pc_abort
	ASM_BUG()
el1_undef:
	/*
	 * Undefined instruction
	 */
	inherit_daif	pstate=x23, tmp=x2
	mov	x0, sp
	bl	do_undefinstr
	kernel_exit 1
el1_dbg:
	/*
	 * Debug exception handling
	 */
	cmp	x24, #ESR_ELx_EC_BRK64		// if BRK64
	cinc	x24, x24, eq			// set bit '0'
	tbz	x24, #0, el1_inv		// EL1 only
	gic_prio_kentry_setup tmp=x3
	mrs	x0, far_el1
	mov	x2, sp				// struct pt_regs
	bl	do_debug_exception
	kernel_exit 1
el1_inv:
	// TODO: add support for undefined instructions in kernel mode
	inherit_daif	pstate=x23, tmp=x2
	mov	x0, sp
	mov	x2, x1
	mov	x1, #BAD_SYNC
	bl	bad_mode
	ASM_BUG()
ENDPROC(el1_sync)

	.align	6
el1_irq:
	kernel_entry 1
	gic_prio_irq_setup pmr=x20, tmp=x1
	enable_da_f

#ifdef CONFIG_ARM64_PSEUDO_NMI
	test_irqs_unmasked	res=x0, pmr=x20
	cbz	x0, 1f
	bl	asm_nmi_enter
1:
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

	irq_handler

#ifdef CONFIG_PREEMPT
	ldr	x24, [tsk, #TSK_TI_PREEMPT]	// get preempt count
alternative_if ARM64_HAS_IRQ_PRIO_MASKING
	/*
	 * DA_F were cleared at start of handling. If anything is set in DAIF,
	 * we come back from an NMI, so skip preemption
	 */
	mrs	x0, daif
	orr	x24, x24, x0
alternative_else_nop_endif
	cbnz	x24, 1f				// preempt count != 0 || NMI return path
	bl	arm64_preempt_schedule_irq	// irq en/disable is done inside
1:
#endif

#ifdef CONFIG_ARM64_PSEUDO_NMI
	/*
	 * When using IRQ priority masking, we can get spurious interrupts while
	 * PMR is set to GIC_PRIO_IRQOFF. An NMI might also have occurred in a
	 * section with interrupts disabled. Skip tracing in those cases.
	 */
	test_irqs_unmasked	res=x0, pmr=x20
	cbz	x0, 1f
	bl	asm_nmi_exit
1:
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#ifdef CONFIG_ARM64_PSEUDO_NMI
	test_irqs_unmasked	res=x0, pmr=x20
	cbnz	x0, 1f
#endif
	bl	trace_hardirqs_on
1:
#endif

	kernel_exit 1
ENDPROC(el1_irq)

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
	cmp	x24, #ESR_ELx_EC_SVE		// SVE access
	b.eq	el0_sve_acc
	cmp	x24, #ESR_ELx_EC_FP_EXC64	// FP/ASIMD exception
	b.eq	el0_fpsimd_exc
	cmp	x24, #ESR_ELx_EC_SYS64		// configurable trap
	ccmp	x24, #ESR_ELx_EC_WFx, #4, ne
	b.eq	el0_sys
	cmp	x24, #ESR_ELx_EC_SP_ALIGN	// stack alignment exception
	b.eq	el0_sp
	cmp	x24, #ESR_ELx_EC_PC_ALIGN	// pc alignment exception
	b.eq	el0_pc
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
	b.eq	el0_pc
	cmp	x24, #ESR_ELx_EC_UNKNOWN	// unknown exception in EL0
	b.eq	el0_undef
	cmp	x24, #ESR_ELx_EC_CP15_32	// CP15 MRC/MCR trap
	b.eq	el0_cp15
	cmp	x24, #ESR_ELx_EC_CP15_64	// CP15 MRRC/MCRR trap
	b.eq	el0_cp15
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
	gic_prio_kentry_setup tmp=x1
	mov	x0, sp
	bl	el0_svc_compat_handler
	b	ret_to_user

	.align	6
el0_irq_compat:
	kernel_entry 0, 32
	b	el0_irq_naked

el0_error_compat:
	kernel_entry 0, 32
	b	el0_error_naked

el0_cp15:
	/*
	 * Trapped CP15 (MRC, MCR, MRRC, MCRR) instructions
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, x25
	mov	x1, sp
	bl	do_cp15instr
	b	ret_to_user
#endif

el0_da:
	/*
	 * Data abort handling
	 */
	mrs	x26, far_el1
	ct_user_exit_irqoff
	enable_daif
	untagged_addr	x0, x26
	mov	x1, x25
	mov	x2, sp
	bl	do_mem_abort
	b	ret_to_user
el0_ia:
	/*
	 * Instruction abort handling
	 */
	mrs	x26, far_el1
	gic_prio_kentry_setup tmp=x0
	ct_user_exit_irqoff
	enable_da_f
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	mov	x0, x26
	mov	x1, x25
	mov	x2, sp
	bl	do_el0_ia_bp_hardening
	b	ret_to_user
el0_fpsimd_acc:
	/*
	 * Floating Point or Advanced SIMD access
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, x25
	mov	x1, sp
	bl	do_fpsimd_acc
	b	ret_to_user
el0_sve_acc:
	/*
	 * Scalable Vector Extension access
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, x25
	mov	x1, sp
	bl	do_sve_acc
	b	ret_to_user
el0_fpsimd_exc:
	/*
	 * Floating Point, Advanced SIMD or SVE exception
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, x25
	mov	x1, sp
	bl	do_fpsimd_exc
	b	ret_to_user
el0_sp:
	ldr	x26, [sp, #S_SP]
	b	el0_sp_pc
el0_pc:
	mrs	x26, far_el1
el0_sp_pc:
	/*
	 * Stack or PC alignment exception handling
	 */
	gic_prio_kentry_setup tmp=x0
	ct_user_exit_irqoff
	enable_da_f
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif
	mov	x0, x26
	mov	x1, x25
	mov	x2, sp
	bl	do_sp_pc_abort
	b	ret_to_user
el0_undef:
	/*
	 * Undefined instruction
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, sp
	bl	do_undefinstr
	b	ret_to_user
el0_sys:
	/*
	 * System instructions, for trapped cache maintenance instructions
	 */
	ct_user_exit_irqoff
	enable_daif
	mov	x0, x25
	mov	x1, sp
	bl	do_sysinstr
	b	ret_to_user
el0_dbg:
	/*
	 * Debug exception handling
	 */
	tbnz	x24, #0, el0_inv		// EL0 only
	mrs	x24, far_el1
	gic_prio_kentry_setup tmp=x3
	ct_user_exit_irqoff
	mov	x0, x24
	mov	x1, x25
	mov	x2, sp
	bl	do_debug_exception
	enable_da_f
	b	ret_to_user
el0_inv:
	ct_user_exit_irqoff
	enable_daif
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
	gic_prio_irq_setup pmr=x20, tmp=x0
	ct_user_exit_irqoff
	enable_da_f

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_off
#endif

#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	tbz	x22, #55, 1f
	bl	do_el0_irq_bp_hardening
1:
#endif
	irq_handler

#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on
#endif
	b	ret_to_user
ENDPROC(el0_irq)

el1_error:
	kernel_entry 1
	mrs	x1, esr_el1
	gic_prio_kentry_setup tmp=x2
	enable_dbg
	mov	x0, sp
	bl	do_serror
	kernel_exit 1
ENDPROC(el1_error)

el0_error:
	kernel_entry 0
el0_error_naked:
	mrs	x25, esr_el1
	gic_prio_kentry_setup tmp=x2
	ct_user_exit_irqoff
	enable_dbg
	mov	x0, sp
	mov	x1, x25
	bl	do_serror
	enable_da_f
	b	ret_to_user
ENDPROC(el0_error)

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
work_pending:
	mov	x0, sp				// 'regs'
	bl	do_notify_resume
#ifdef CONFIG_TRACE_IRQFLAGS
	bl	trace_hardirqs_on		// enabled while in userspace
#endif
	ldr	x1, [tsk, #TSK_TI_FLAGS]	// re-check for single-step
	b	finish_ret_to_user
/*
 * "slow" syscall return path.
 */
ret_to_user:
	disable_daif
	gic_prio_kentry_setup tmp=x3
	ldr	x1, [tsk, #TSK_TI_FLAGS]
	and	x2, x1, #_TIF_WORK_MASK
	cbnz	x2, work_pending
finish_ret_to_user:
	enable_step_tsk x1, x2
#ifdef CONFIG_GCC_PLUGIN_STACKLEAK
	bl	stackleak_erase
#endif
	kernel_exit 0
ENDPROC(ret_to_user)

/*
 * SVC handler.
 */
	.align	6
el0_svc:
	gic_prio_kentry_setup tmp=x1
	mov	x0, sp
	bl	el0_svc_handler
	b	ret_to_user
ENDPROC(el0_svc)

	.popsection				// .entry.text

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
/*
 * Exception vectors trampoline.
 */
	.pushsection ".entry.tramp.text", "ax"

	.macro tramp_map_kernel, tmp
	mrs	\tmp, ttbr1_el1
	add	\tmp, \tmp, #(PAGE_SIZE + RESERVED_TTBR0_SIZE)
	bic	\tmp, \tmp, #USER_ASID_FLAG
	msr	ttbr1_el1, \tmp
#ifdef CONFIG_QCOM_FALKOR_ERRATUM_1003
alternative_if ARM64_WORKAROUND_QCOM_FALKOR_E1003
	/* ASID already in \tmp[63:48] */
	movk	\tmp, #:abs_g2_nc:(TRAMP_VALIAS >> 12)
	movk	\tmp, #:abs_g1_nc:(TRAMP_VALIAS >> 12)
	/* 2MB boundary containing the vectors, so we nobble the walk cache */
	movk	\tmp, #:abs_g0_nc:((TRAMP_VALIAS & ~(SZ_2M - 1)) >> 12)
	isb
	tlbi	vae1, \tmp
	dsb	nsh
alternative_else_nop_endif
#endif /* CONFIG_QCOM_FALKOR_ERRATUM_1003 */
	.endm

	.macro tramp_unmap_kernel, tmp
	mrs	\tmp, ttbr1_el1
	sub	\tmp, \tmp, #(PAGE_SIZE + RESERVED_TTBR0_SIZE)
	orr	\tmp, \tmp, #USER_ASID_FLAG
	msr	ttbr1_el1, \tmp
	/*
	 * We avoid running the post_ttbr_update_workaround here because
	 * it's only needed by Cavium ThunderX, which requires KPTI to be
	 * disabled.
	 */
	.endm

	.macro tramp_ventry, regsize = 64
	.align	7
1:
	.if	\regsize == 64
	msr	tpidrro_el0, x30	// Restored in kernel_ventry
	.endif
	/*
	 * Defend against branch aliasing attacks by pushing a dummy
	 * entry onto the return stack and using a RET instruction to
	 * enter the full-fat kernel vectors.
	 */
	bl	2f
	b	.
2:
	tramp_map_kernel	x30
#ifdef CONFIG_RANDOMIZE_BASE
	adr	x30, tramp_vectors + PAGE_SIZE
alternative_insn isb, nop, ARM64_WORKAROUND_QCOM_FALKOR_E1003
	ldr	x30, [x30]
#else
	ldr	x30, =vectors
#endif
alternative_if_not ARM64_WORKAROUND_CAVIUM_TX2_219_PRFM
	prfm	plil1strm, [x30, #(1b - tramp_vectors)]
alternative_else_nop_endif
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
	sb
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
	msr	sp_el0, x1
#ifdef CONFIG_SHADOW_CALL_STACK
	str	x18, [x0, #TSK_TI_SCS]
	ldr	x18, [x1, #TSK_TI_SCS]
	str	xzr, [x1, #TSK_TI_SCS]		// limit visibility of saved SCS
#endif
	ret
ENDPROC(cpu_switch_to)
NOKPROBE(cpu_switch_to)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cbz	x19, 1f				// not a kernel thread
	mov	x0, x20
	blr	x19
1:	get_current_task tsk
	b	ret_to_user
ENDPROC(ret_from_fork)
NOKPROBE(ret_from_fork)

#ifdef CONFIG_ARM_SDE_INTERFACE

#include <asm/sdei.h>
#include <uapi/linux/arm_sdei.h>

.macro sdei_handler_exit exit_mode
	/* On success, this call never returns... */
	cmp	\exit_mode, #SDEI_EXIT_SMC
	b.ne	99f
	smc	#0
	b	.
99:	hvc	#0
	b	.
.endm

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
/*
 * The regular SDEI entry point may have been unmapped along with the rest of
 * the kernel. This trampoline restores the kernel mapping to make the x1 memory
 * argument accessible.
 *
 * This clobbers x4, __sdei_handler() will restore this from firmware's
 * copy.
 */
.ltorg
.pushsection ".entry.tramp.text", "ax"
ENTRY(__sdei_asm_entry_trampoline)
	mrs	x4, ttbr1_el1
	tbz	x4, #USER_ASID_BIT, 1f

	tramp_map_kernel tmp=x4
	isb
	mov	x4, xzr

	/*
	 * Use reg->interrupted_regs.addr_limit to remember whether to unmap
	 * the kernel on exit.
	 */
1:	str	x4, [x1, #(SDEI_EVENT_INTREGS + S_ORIG_ADDR_LIMIT)]

#ifdef CONFIG_RANDOMIZE_BASE
	adr	x4, tramp_vectors + PAGE_SIZE
	add	x4, x4, #:lo12:__sdei_asm_trampoline_next_handler
	ldr	x4, [x4]
#else
	ldr	x4, =__sdei_asm_handler
#endif
	br	x4
ENDPROC(__sdei_asm_entry_trampoline)
NOKPROBE(__sdei_asm_entry_trampoline)

/*
 * Make the exit call and restore the original ttbr1_el1
 *
 * x0 & x1: setup for the exit API call
 * x2: exit_mode
 * x4: struct sdei_registered_event argument from registration time.
 */
ENTRY(__sdei_asm_exit_trampoline)
	ldr	x4, [x4, #(SDEI_EVENT_INTREGS + S_ORIG_ADDR_LIMIT)]
	cbnz	x4, 1f

	tramp_unmap_kernel	tmp=x4

1:	sdei_handler_exit exit_mode=x2
ENDPROC(__sdei_asm_exit_trampoline)
NOKPROBE(__sdei_asm_exit_trampoline)
	.ltorg
.popsection		// .entry.tramp.text
#ifdef CONFIG_RANDOMIZE_BASE
.pushsection ".rodata", "a"
__sdei_asm_trampoline_next_handler:
	.quad	__sdei_asm_handler
.popsection		// .rodata
#endif /* CONFIG_RANDOMIZE_BASE */
#endif /* CONFIG_UNMAP_KERNEL_AT_EL0 */

/*
 * Software Delegated Exception entry point.
 *
 * x0: Event number
 * x1: struct sdei_registered_event argument from registration time.
 * x2: interrupted PC
 * x3: interrupted PSTATE
 * x4: maybe clobbered by the trampoline
 *
 * Firmware has preserved x0->x17 for us, we must save/restore the rest to
 * follow SMC-CC. We save (or retrieve) all the registers as the handler may
 * want them.
 */
ENTRY(__sdei_asm_handler)
	stp     x2, x3, [x1, #SDEI_EVENT_INTREGS + S_PC]
	stp     x4, x5, [x1, #SDEI_EVENT_INTREGS + 16 * 2]
	stp     x6, x7, [x1, #SDEI_EVENT_INTREGS + 16 * 3]
	stp     x8, x9, [x1, #SDEI_EVENT_INTREGS + 16 * 4]
	stp     x10, x11, [x1, #SDEI_EVENT_INTREGS + 16 * 5]
	stp     x12, x13, [x1, #SDEI_EVENT_INTREGS + 16 * 6]
	stp     x14, x15, [x1, #SDEI_EVENT_INTREGS + 16 * 7]
	stp     x16, x17, [x1, #SDEI_EVENT_INTREGS + 16 * 8]
	stp     x18, x19, [x1, #SDEI_EVENT_INTREGS + 16 * 9]
	stp     x20, x21, [x1, #SDEI_EVENT_INTREGS + 16 * 10]
	stp     x22, x23, [x1, #SDEI_EVENT_INTREGS + 16 * 11]
	stp     x24, x25, [x1, #SDEI_EVENT_INTREGS + 16 * 12]
	stp     x26, x27, [x1, #SDEI_EVENT_INTREGS + 16 * 13]
	stp     x28, x29, [x1, #SDEI_EVENT_INTREGS + 16 * 14]
	mov	x4, sp
	stp     lr, x4, [x1, #SDEI_EVENT_INTREGS + S_LR]

	mov	x19, x1

#ifdef CONFIG_VMAP_STACK
	/*
	 * entry.S may have been using sp as a scratch register, find whether
	 * this is a normal or critical event and switch to the appropriate
	 * stack for this CPU.
	 */
	ldrb	w4, [x19, #SDEI_EVENT_PRIORITY]
	cbnz	w4, 1f
	ldr_this_cpu dst=x5, sym=sdei_stack_normal_ptr, tmp=x6
	b	2f
1:	ldr_this_cpu dst=x5, sym=sdei_stack_critical_ptr, tmp=x6
2:	mov	x6, #SDEI_STACK_SIZE
	add	x5, x5, x6
	mov	sp, x5
#endif

	/*
	 * We may have interrupted userspace, or a guest, or exit-from or
	 * return-to either of these. We can't trust sp_el0, restore it.
	 */
	mrs	x28, sp_el0
	ldr_this_cpu	dst=x0, sym=__entry_task, tmp=x1
	msr	sp_el0, x0

	/* If we interrupted the kernel point to the previous stack/frame. */
	and     x0, x3, #0xc
	mrs     x1, CurrentEL
	cmp     x0, x1
	csel	x29, x29, xzr, eq	// fp, or zero
	csel	x4, x2, xzr, eq		// elr, or zero

	stp	x29, x4, [sp, #-16]!
	mov	x29, sp

	add	x0, x19, #SDEI_EVENT_INTREGS
	mov	x1, x19
	bl	__sdei_handler

	msr	sp_el0, x28
	/* restore regs >x17 that we clobbered */
	mov	x4, x19         // keep x4 for __sdei_asm_exit_trampoline
	ldp	x28, x29, [x4, #SDEI_EVENT_INTREGS + 16 * 14]
	ldp	x18, x19, [x4, #SDEI_EVENT_INTREGS + 16 * 9]
	ldp	lr, x1, [x4, #SDEI_EVENT_INTREGS + S_LR]
	mov	sp, x1

	mov	x1, x0			// address to complete_and_resume
	/* x0 = (x0 <= 1) ? EVENT_COMPLETE:EVENT_COMPLETE_AND_RESUME */
	cmp	x0, #1
	mov_q	x2, SDEI_1_0_FN_SDEI_EVENT_COMPLETE
	mov_q	x3, SDEI_1_0_FN_SDEI_EVENT_COMPLETE_AND_RESUME
	csel	x0, x2, x3, ls

	ldr_l	x2, sdei_exit_mode

alternative_if_not ARM64_UNMAP_KERNEL_AT_EL0
	sdei_handler_exit exit_mode=x2
alternative_else_nop_endif

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
	tramp_alias	dst=x5, sym=__sdei_asm_exit_trampoline
	br	x5
#endif
ENDPROC(__sdei_asm_handler)
NOKPROBE(__sdei_asm_handler)
#endif /* CONFIG_ARM_SDE_INTERFACE */
