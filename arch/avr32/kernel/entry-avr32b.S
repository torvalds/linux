/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This file contains the low-level entry-points into the kernel, that is,
 * exception handlers, debug trap handlers, interrupt handlers and the
 * system call handler.
 */
#include <linux/errno.h>

#include <asm/asm.h>
#include <asm/hardirq.h>
#include <asm/irq.h>
#include <asm/ocd.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/ptrace.h>
#include <asm/sysreg.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

#ifdef CONFIG_PREEMPT
# define preempt_stop		mask_interrupts
#else
# define preempt_stop
# define fault_resume_kernel	fault_restore_all
#endif

#define __MASK(x)	((1 << (x)) - 1)
#define IRQ_MASK	((__MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT) | \
			 (__MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT))

	.section .ex.text,"ax",@progbits
	.align	2
exception_vectors:
	bral	handle_critical
	.align	2
	bral	handle_critical
	.align	2
	bral	do_bus_error_write
	.align	2
	bral	do_bus_error_read
	.align	2
	bral	do_nmi_ll
	.align	2
	bral	handle_address_fault
	.align	2
	bral	handle_protection_fault
	.align	2
	bral	handle_debug
	.align	2
	bral	do_illegal_opcode_ll
	.align	2
	bral	do_illegal_opcode_ll
	.align	2
	bral	do_illegal_opcode_ll
	.align	2
	bral	do_fpe_ll
	.align	2
	bral	do_illegal_opcode_ll
	.align	2
	bral	handle_address_fault
	.align	2
	bral	handle_address_fault
	.align	2
	bral	handle_protection_fault
	.align	2
	bral	handle_protection_fault
	.align	2
	bral	do_dtlb_modified

#define	tlbmiss_save	pushm	r0-r3
#define tlbmiss_restore	popm	r0-r3

	.org	0x50
	.global	itlb_miss
itlb_miss:
	tlbmiss_save
	rjmp	tlb_miss_common

	.org	0x60
dtlb_miss_read:
	tlbmiss_save
	rjmp	tlb_miss_common

	.org	0x70
dtlb_miss_write:
	tlbmiss_save

	.global	tlb_miss_common
	.align	2
tlb_miss_common:
	mfsr	r0, SYSREG_TLBEAR
	mfsr	r1, SYSREG_PTBR

	/*
	 * First level lookup: The PGD contains virtual pointers to
	 * the second-level page tables, but they may be NULL if not
	 * present.
	 */
pgtbl_lookup:
	lsr	r2, r0, PGDIR_SHIFT
	ld.w	r3, r1[r2 << 2]
	bfextu	r1, r0, PAGE_SHIFT, PGDIR_SHIFT - PAGE_SHIFT
	cp.w	r3, 0
	breq	page_table_not_present

	/* Second level lookup */
	ld.w	r2, r3[r1 << 2]
	mfsr	r0, SYSREG_TLBARLO
	bld	r2, _PAGE_BIT_PRESENT
	brcc	page_not_present

	/* Mark the page as accessed */
	sbr	r2, _PAGE_BIT_ACCESSED
	st.w	r3[r1 << 2], r2

	/* Drop software flags */
	andl	r2, _PAGE_FLAGS_HARDWARE_MASK & 0xffff
	mtsr	SYSREG_TLBELO, r2

	/* Figure out which entry we want to replace */
	mfsr	r1, SYSREG_MMUCR
	clz	r2, r0
	brcc	1f
	mov	r3, -1			/* All entries have been accessed, */
	mov	r2, 0			/* so start at 0 */
	mtsr	SYSREG_TLBARLO, r3	/* and reset TLBAR */

1:	bfins	r1, r2, SYSREG_DRP_OFFSET, SYSREG_DRP_SIZE
	mtsr	SYSREG_MMUCR, r1
	tlbw

	tlbmiss_restore
	rete

	/* The slow path of the TLB miss handler */
	.align	2
page_table_not_present:
	/* Do we need to synchronize with swapper_pg_dir? */
	bld	r0, 31
	brcs	sync_with_swapper_pg_dir

page_not_present:
	tlbmiss_restore
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_page_fault
	rjmp	ret_from_exception

	.align	2
sync_with_swapper_pg_dir:
	/*
	 * If swapper_pg_dir contains a non-NULL second-level page
	 * table pointer, copy it into the current PGD. If not, we
	 * must handle it as a full-blown page fault.
	 *
	 * Jumping back to pgtbl_lookup causes an unnecessary lookup,
	 * but it is guaranteed to be a cache hit, it won't happen
	 * very often, and we absolutely do not want to sacrifice any
	 * performance in the fast path in order to improve this.
	 */
	mov	r1, lo(swapper_pg_dir)
	orh	r1, hi(swapper_pg_dir)
	ld.w	r3, r1[r2 << 2]
	cp.w	r3, 0
	breq	page_not_present
	mfsr	r1, SYSREG_PTBR
	st.w	r1[r2 << 2], r3
	rjmp	pgtbl_lookup

	/*
	 * We currently have two bytes left at this point until we
	 * crash into the system call handler...
	 *
	 * Don't worry, the assembler will let us know.
	 */


	/* ---                    System Call                    --- */

	.org	0x100
system_call:
#ifdef CONFIG_PREEMPT
	mask_interrupts
#endif
	pushm	r12		/* r12_orig */
	stmts	--sp, r0-lr

	mfsr	r0, SYSREG_RAR_SUP
	mfsr	r1, SYSREG_RSR_SUP
#ifdef CONFIG_PREEMPT
	unmask_interrupts
#endif
	zero_fp
	stm	--sp, r0-r1

	/* check for syscall tracing */
	get_thread_info r0
	ld.w	r1, r0[TI_flags]
	bld	r1, TIF_SYSCALL_TRACE
	brcs	syscall_trace_enter

syscall_trace_cont:
	cp.w	r8, NR_syscalls
	brhs	syscall_badsys

	lddpc   lr, syscall_table_addr
	ld.w    lr, lr[r8 << 2]
	mov	r8, r5		/* 5th argument (6th is pushed by stub) */
	icall   lr

	.global	syscall_return
syscall_return:
	get_thread_info r0
	mask_interrupts		/* make sure we don't miss an interrupt
				   setting need_resched or sigpending
				   between sampling and the rets */

	/* Store the return value so that the correct value is loaded below */
	stdsp   sp[REG_R12], r12

	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_ALLWORK_MASK, COH
	brne	syscall_exit_work

syscall_exit_cont:
	popm	r8-r9
	mtsr	SYSREG_RAR_SUP, r8
	mtsr	SYSREG_RSR_SUP, r9
	ldmts	sp++, r0-lr
	sub	sp, -4		/* r12_orig */
	rets

	.align	2
syscall_table_addr:
	.long   sys_call_table

syscall_badsys:
	mov	r12, -ENOSYS
	rjmp	syscall_return

	.global ret_from_fork
ret_from_fork:
	call   schedule_tail

	/* check for syscall tracing */
	get_thread_info r0
	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_ALLWORK_MASK, COH
	brne	syscall_exit_work
	rjmp    syscall_exit_cont

syscall_trace_enter:
	pushm	r8-r12
	call	syscall_trace
	popm	r8-r12
	rjmp	syscall_trace_cont

syscall_exit_work:
	bld	r1, TIF_SYSCALL_TRACE
	brcc	1f
	unmask_interrupts
	call	syscall_trace
	mask_interrupts
	ld.w	r1, r0[TI_flags]

1:	bld	r1, TIF_NEED_RESCHED
	brcc	2f
	unmask_interrupts
	call	schedule
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

2:	mov	r2, _TIF_SIGPENDING | _TIF_RESTORE_SIGMASK | _TIF_NOTIFY_RESUME
	tst	r1, r2
	breq	3f
	unmask_interrupts
	mov	r12, sp
	mov	r11, r0
	call	do_notify_resume
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

3:	bld	r1, TIF_BREAKPOINT
	brcc	syscall_exit_cont
	rjmp	enter_monitor_mode

	/* This function expects to find offending PC in SYSREG_RAR_EX */
	.type	save_full_context_ex, @function
	.align	2
save_full_context_ex:
	mfsr	r11, SYSREG_RAR_EX
	sub	r9, pc, . - debug_trampoline
	mfsr	r8, SYSREG_RSR_EX
	cp.w	r9, r11
	breq	3f
	mov	r12, r8
	andh	r8, (MODE_MASK >> 16), COH
	brne	2f

1:	pushm	r11, r12	/* PC and SR */
	unmask_exceptions
	ret	r12

2:	sub	r10, sp, -(FRAME_SIZE_FULL - REG_LR)
	stdsp	sp[4], r10	/* replace saved SP */
	rjmp	1b

	/*
	 * The debug handler set up a trampoline to make us
	 * automatically enter monitor mode upon return, but since
	 * we're saving the full context, we must assume that the
	 * exception handler might want to alter the return address
	 * and/or status register. So we need to restore the original
	 * context and enter monitor mode manually after the exception
	 * has been handled.
	 */
3:	get_thread_info r8
	ld.w	r11, r8[TI_rar_saved]
	ld.w	r12, r8[TI_rsr_saved]
	rjmp	1b
	.size	save_full_context_ex, . - save_full_context_ex

	/* Low-level exception handlers */
handle_critical:
	/*
	 * AT32AP700x errata:
	 *
	 * After a Java stack overflow or underflow trap, any CPU
	 * memory access may cause erratic behavior. This will happen
	 * when the four least significant bits of the JOSP system
	 * register contains any value between 9 and 15 (inclusive).
	 *
	 * Possible workarounds:
	 *   - Don't use the Java Extension Module
	 *   - Ensure that the stack overflow and underflow trap
	 *     handlers do not do any memory access or trigger any
	 *     exceptions before the overflow/underflow condition is
	 *     cleared (by incrementing or decrementing the JOSP)
	 *   - Make sure that JOSP does not contain any problematic
	 *     value before doing any exception or interrupt
	 *     processing.
	 *   - Set up a critical exception handler which writes a
	 *     known-to-be-safe value, e.g. 4, to JOSP before doing
	 *     any further processing.
	 *
	 * We'll use the last workaround for now since we cannot
	 * guarantee that user space processes don't use Java mode.
	 * Non-well-behaving userland will be terminated with extreme
	 * prejudice.
	 */
#ifdef CONFIG_CPU_AT32AP700X
	/*
	 * There's a chance we can't touch memory, so temporarily
	 * borrow PTBR to save the stack pointer while we fix things
	 * up...
	 */
	mtsr	SYSREG_PTBR, sp
	mov	sp, 4
	mtsr	SYSREG_JOSP, sp
	mfsr	sp, SYSREG_PTBR
	sub	pc, -2

	/* Push most of pt_regs on stack. We'll do the rest later */
	sub	sp, 4
	pushm	r0-r12

	/* PTBR mirrors current_thread_info()->task->active_mm->pgd */
	get_thread_info r0
	ld.w	r1, r0[TI_task]
	ld.w	r2, r1[TSK_active_mm]
	ld.w	r3, r2[MM_pgd]
	mtsr	SYSREG_PTBR, r3
#else
	sub	sp, 4
	pushm	r0-r12
#endif
	sub	r0, sp, -(14 * 4)
	mov	r1, lr
	mfsr	r2, SYSREG_RAR_EX
	mfsr	r3, SYSREG_RSR_EX
	pushm	r0-r3

	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_critical_exception

	/* We should never get here... */
bad_return:
	sub	r12, pc, (. - 1f)
	bral	panic
	.align	2
1:	.asciz	"Return from critical exception!"

	.align	1
do_bus_error_write:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mov	r11, 1
	rjmp	1f

do_bus_error_read:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mov	r11, 0
1:	mfsr	r12, SYSREG_BEAR
	mov	r10, sp
	call	do_bus_error
	rjmp	ret_from_exception

	.align	1
do_nmi_ll:
	sub	sp, 4
	stmts	--sp, r0-lr
	mfsr	r9, SYSREG_RSR_NMI
	mfsr	r8, SYSREG_RAR_NMI
	bfextu	r0, r9, MODE_SHIFT, 3
	brne	2f

1:	pushm	r8, r9	/* PC and SR */
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_nmi
	popm	r8-r9
	mtsr	SYSREG_RAR_NMI, r8
	tst	r0, r0
	mtsr	SYSREG_RSR_NMI, r9
	brne	3f

	ldmts	sp++, r0-lr
	sub	sp, -4		/* skip r12_orig */
	rete

2:	sub	r10, sp, -(FRAME_SIZE_FULL - REG_LR)
	stdsp	sp[4], r10	/* replace saved SP */
	rjmp	1b

3:	popm	lr
	sub	sp, -4		/* skip sp */
	popm	r0-r12
	sub	sp, -4		/* skip r12_orig */
	rete

handle_address_fault:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_address_exception
	rjmp	ret_from_exception

handle_protection_fault:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_page_fault
	rjmp	ret_from_exception

	.align	1
do_illegal_opcode_ll:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	call	do_illegal_opcode
	rjmp	ret_from_exception

do_dtlb_modified:
	pushm	r0-r3
	mfsr	r1, SYSREG_TLBEAR
	mfsr	r0, SYSREG_PTBR
	lsr	r2, r1, PGDIR_SHIFT
	ld.w	r0, r0[r2 << 2]
	lsl	r1, (32 - PGDIR_SHIFT)
	lsr	r1, (32 - PGDIR_SHIFT) + PAGE_SHIFT

	/* Translate to virtual address in P1 */
	andl	r0, 0xf000
	sbr	r0, 31
	add	r2, r0, r1 << 2
	ld.w	r3, r2[0]
	sbr	r3, _PAGE_BIT_DIRTY
	mov	r0, r3
	st.w	r2[0], r3

	/* The page table is up-to-date. Update the TLB entry as well */
	andl	r0, lo(_PAGE_FLAGS_HARDWARE_MASK)
	mtsr	SYSREG_TLBELO, r0

	/* MMUCR[DRP] is updated automatically, so let's go... */
	tlbw

	popm	r0-r3
	rete

do_fpe_ll:
	sub	sp, 4
	stmts	--sp, r0-lr
	call	save_full_context_ex
	unmask_interrupts
	mov	r12, 26
	mov	r11, sp
	call	do_fpe
	rjmp	ret_from_exception

ret_from_exception:
	mask_interrupts
	lddsp	r4, sp[REG_SR]

	andh	r4, (MODE_MASK >> 16), COH
	brne	fault_resume_kernel

	get_thread_info r0
	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_WORK_MASK, COH
	brne	fault_exit_work

fault_resume_user:
	popm	r8-r9
	mask_exceptions
	mtsr	SYSREG_RAR_EX, r8
	mtsr	SYSREG_RSR_EX, r9
	ldmts	sp++, r0-lr
	sub	sp, -4
	rete

fault_resume_kernel:
#ifdef CONFIG_PREEMPT
	get_thread_info	r0
	ld.w	r2, r0[TI_preempt_count]
	cp.w	r2, 0
	brne	1f
	ld.w	r1, r0[TI_flags]
	bld	r1, TIF_NEED_RESCHED
	brcc	1f
	lddsp	r4, sp[REG_SR]
	bld	r4, SYSREG_GM_OFFSET
	brcs	1f
	call	preempt_schedule_irq
1:
#endif

	popm	r8-r9
	mask_exceptions
	mfsr	r1, SYSREG_SR
	mtsr	SYSREG_RAR_EX, r8
	mtsr	SYSREG_RSR_EX, r9
	popm	lr
	sub	sp, -4		/* ignore SP */
	popm	r0-r12
	sub	sp, -4		/* ignore r12_orig */
	rete

irq_exit_work:
	/* Switch to exception mode so that we can share the same code. */
	mfsr	r8, SYSREG_SR
	cbr	r8, SYSREG_M0_OFFSET
	orh	r8, hi(SYSREG_BIT(M1) | SYSREG_BIT(M2))
	mtsr	SYSREG_SR, r8
	sub	pc, -2
	get_thread_info r0
	ld.w	r1, r0[TI_flags]

fault_exit_work:
	bld	r1, TIF_NEED_RESCHED
	brcc	1f
	unmask_interrupts
	call	schedule
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	fault_exit_work

1:	mov	r2, _TIF_SIGPENDING | _TIF_RESTORE_SIGMASK
	tst	r1, r2
	breq	2f
	unmask_interrupts
	mov	r12, sp
	mov	r11, r0
	call	do_notify_resume
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	fault_exit_work

2:	bld	r1, TIF_BREAKPOINT
	brcc	fault_resume_user
	rjmp	enter_monitor_mode

	.section .kprobes.text, "ax", @progbits
	.type	handle_debug, @function
handle_debug:
	sub	sp, 4		/* r12_orig */
	stmts	--sp, r0-lr
	mfsr	r8, SYSREG_RAR_DBG
	mfsr	r9, SYSREG_RSR_DBG
	unmask_exceptions
	pushm	r8-r9
	bfextu	r9, r9, SYSREG_MODE_OFFSET, SYSREG_MODE_SIZE
	brne	debug_fixup_regs

.Ldebug_fixup_cont:
#ifdef CONFIG_TRACE_IRQFLAGS
	call	trace_hardirqs_off
#endif
	mov	r12, sp
	call	do_debug
	mov	sp, r12

	lddsp	r2, sp[REG_SR]
	bfextu	r3, r2, SYSREG_MODE_OFFSET, SYSREG_MODE_SIZE
	brne	debug_resume_kernel

	get_thread_info r0
	ld.w	r1, r0[TI_flags]
	mov	r2, _TIF_DBGWORK_MASK
	tst	r1, r2
	brne	debug_exit_work

	bld	r1, TIF_SINGLE_STEP
	brcc	1f
	mfdr	r4, OCD_DC
	sbr	r4, OCD_DC_SS_BIT
	mtdr	OCD_DC, r4

1:	popm	r10,r11
	mask_exceptions
	mtsr	SYSREG_RSR_DBG, r11
	mtsr	SYSREG_RAR_DBG, r10
#ifdef CONFIG_TRACE_IRQFLAGS
	call	trace_hardirqs_on
1:
#endif
	ldmts	sp++, r0-lr
	sub	sp, -4
	retd
	.size	handle_debug, . - handle_debug

	/* Mode of the trapped context is in r9 */
	.type	debug_fixup_regs, @function
debug_fixup_regs:
	mfsr	r8, SYSREG_SR
	mov	r10, r8
	bfins	r8, r9, SYSREG_MODE_OFFSET, SYSREG_MODE_SIZE
	mtsr	SYSREG_SR, r8
	sub	pc, -2
	stdsp	sp[REG_LR], lr
	mtsr	SYSREG_SR, r10
	sub	pc, -2
	sub	r8, sp, -FRAME_SIZE_FULL
	stdsp	sp[REG_SP], r8
	rjmp	.Ldebug_fixup_cont
	.size	debug_fixup_regs, . - debug_fixup_regs

	.type	debug_resume_kernel, @function
debug_resume_kernel:
	mask_exceptions
	popm	r10, r11
	mtsr	SYSREG_RAR_DBG, r10
	mtsr	SYSREG_RSR_DBG, r11
#ifdef CONFIG_TRACE_IRQFLAGS
	bld	r11, SYSREG_GM_OFFSET
	brcc	1f
	call	trace_hardirqs_on
1:
#endif
	mfsr	r2, SYSREG_SR
	mov	r1, r2
	bfins	r2, r3, SYSREG_MODE_OFFSET, SYSREG_MODE_SIZE
	mtsr	SYSREG_SR, r2
	sub	pc, -2
	popm	lr
	mtsr	SYSREG_SR, r1
	sub	pc, -2
	sub	sp, -4		/* skip SP */
	popm	r0-r12
	sub	sp, -4
	retd
	.size	debug_resume_kernel, . - debug_resume_kernel

	.type	debug_exit_work, @function
debug_exit_work:
	/*
	 * We must return from Monitor Mode using a retd, and we must
	 * not schedule since that involves the D bit in SR getting
	 * cleared by something other than the debug hardware. This
	 * may cause undefined behaviour according to the Architecture
	 * manual.
	 *
	 * So we fix up the return address and status and return to a
	 * stub below in Exception mode. From there, we can follow the
	 * normal exception return path.
	 *
	 * The real return address and status registers are stored on
	 * the stack in the way the exception return path understands,
	 * so no need to fix anything up there.
	 */
	sub	r8, pc, . - fault_exit_work
	mtsr	SYSREG_RAR_DBG, r8
	mov	r9, 0
	orh	r9, hi(SR_EM | SR_GM | MODE_EXCEPTION)
	mtsr	SYSREG_RSR_DBG, r9
	sub	pc, -2
	retd
	.size	debug_exit_work, . - debug_exit_work

	.set	rsr_int0,	SYSREG_RSR_INT0
	.set	rsr_int1,	SYSREG_RSR_INT1
	.set	rsr_int2,	SYSREG_RSR_INT2
	.set	rsr_int3,	SYSREG_RSR_INT3
	.set	rar_int0,	SYSREG_RAR_INT0
	.set	rar_int1,	SYSREG_RAR_INT1
	.set	rar_int2,	SYSREG_RAR_INT2
	.set	rar_int3,	SYSREG_RAR_INT3

	.macro	IRQ_LEVEL level
	.type	irq_level\level, @function
irq_level\level:
	sub	sp, 4		/* r12_orig */
	stmts	--sp,r0-lr
	mfsr	r8, rar_int\level
	mfsr	r9, rsr_int\level

#ifdef CONFIG_PREEMPT
	sub	r11, pc, (. - system_call)
	cp.w	r11, r8
	breq	4f
#endif

	pushm	r8-r9

	mov	r11, sp
	mov	r12, \level

	call	do_IRQ

	lddsp	r4, sp[REG_SR]
	bfextu	r4, r4, SYSREG_M0_OFFSET, 3
	cp.w	r4, MODE_SUPERVISOR >> SYSREG_M0_OFFSET
	breq	2f
	cp.w	r4, MODE_USER >> SYSREG_M0_OFFSET
#ifdef CONFIG_PREEMPT
	brne	3f
#else
	brne	1f
#endif

	get_thread_info	r0
	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_WORK_MASK, COH
	brne	irq_exit_work

1:
#ifdef CONFIG_TRACE_IRQFLAGS
	call	trace_hardirqs_on
#endif
	popm	r8-r9
	mtsr	rar_int\level, r8
	mtsr	rsr_int\level, r9
	ldmts	sp++,r0-lr
	sub	sp, -4		/* ignore r12_orig */
	rete

#ifdef CONFIG_PREEMPT
4:	mask_interrupts
	mfsr	r8, rsr_int\level
	sbr	r8, 16
	mtsr	rsr_int\level, r8
	ldmts	sp++, r0-lr
	sub	sp, -4		/* ignore r12_orig */
	rete
#endif

2:	get_thread_info	r0
	ld.w	r1, r0[TI_flags]
	bld	r1, TIF_CPU_GOING_TO_SLEEP
#ifdef CONFIG_PREEMPT
	brcc	3f
#else
	brcc	1b
#endif
	sub	r1, pc, . - cpu_idle_skip_sleep
	stdsp	sp[REG_PC], r1
#ifdef CONFIG_PREEMPT
3:	get_thread_info r0
	ld.w	r2, r0[TI_preempt_count]
	cp.w	r2, 0
	brne	1b
	ld.w	r1, r0[TI_flags]
	bld	r1, TIF_NEED_RESCHED
	brcc	1b
	lddsp	r4, sp[REG_SR]
	bld	r4, SYSREG_GM_OFFSET
	brcs	1b
	call	preempt_schedule_irq
#endif
	rjmp	1b
	.endm

	.section .irq.text,"ax",@progbits

	.global	irq_level0
	.global	irq_level1
	.global	irq_level2
	.global	irq_level3
	IRQ_LEVEL 0
	IRQ_LEVEL 1
	IRQ_LEVEL 2
	IRQ_LEVEL 3

	.section .kprobes.text, "ax", @progbits
	.type	enter_monitor_mode, @function
enter_monitor_mode:
	/*
	 * We need to enter monitor mode to do a single step. The
	 * monitor code will alter the return address so that we
	 * return directly to the user instead of returning here.
	 */
	breakpoint
	rjmp	breakpoint_failed

	.size	enter_monitor_mode, . - enter_monitor_mode

	.type	debug_trampoline, @function
	.global	debug_trampoline
debug_trampoline:
	/*
	 * Save the registers on the stack so that the monitor code
	 * can find them easily.
	 */
	sub	sp, 4		/* r12_orig */
	stmts	--sp, r0-lr
	get_thread_info	r0
	ld.w	r8, r0[TI_rar_saved]
	ld.w	r9, r0[TI_rsr_saved]
	pushm	r8-r9

	/*
	 * The monitor code will alter the return address so we don't
	 * return here.
	 */
	breakpoint
	rjmp	breakpoint_failed
	.size	debug_trampoline, . - debug_trampoline

	.type breakpoint_failed, @function
breakpoint_failed:
	/*
	 * Something went wrong. Perhaps the debug hardware isn't
	 * enabled?
	 */
	lda.w	r12, msg_breakpoint_failed
	mov	r11, sp
	mov	r10, 9		/* SIGKILL */
	call	die
1:	rjmp	1b

msg_breakpoint_failed:
	.asciz	"Failed to enter Debug Mode"
