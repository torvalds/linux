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

	/*
	 * r0 :	PGD/PT/PTE
	 * r1 : Offending address
	 * r2 : Scratch register
	 * r3 : Cause (5, 12 or 13)
	 */
#define	tlbmiss_save	pushm	r0-r3
#define tlbmiss_restore	popm	r0-r3

	.section .tlbx.ex.text,"ax",@progbits
	.global	itlb_miss
itlb_miss:
	tlbmiss_save
	rjmp	tlb_miss_common

	.section .tlbr.ex.text,"ax",@progbits
dtlb_miss_read:
	tlbmiss_save
	rjmp	tlb_miss_common

	.section .tlbw.ex.text,"ax",@progbits
dtlb_miss_write:
	tlbmiss_save

	.global	tlb_miss_common
tlb_miss_common:
	mfsr	r0, SYSREG_TLBEAR
	mfsr	r1, SYSREG_PTBR

	/* Is it the vmalloc space? */
	bld	r0, 31
	brcs	handle_vmalloc_miss

	/* First level lookup */
pgtbl_lookup:
	lsr	r2, r0, PGDIR_SHIFT
	ld.w	r3, r1[r2 << 2]
	bfextu	r1, r0, PAGE_SHIFT, PGDIR_SHIFT - PAGE_SHIFT
	bld	r3, _PAGE_BIT_PRESENT
	brcc	page_table_not_present

	/* Translate to virtual address in P1. */
	andl	r3, 0xf000
	sbr	r3, 31

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

handle_vmalloc_miss:
	/* Simply do the lookup in init's page table */
	mov	r1, lo(swapper_pg_dir)
	orh	r1, hi(swapper_pg_dir)
	rjmp	pgtbl_lookup


	/* ---                    System Call                    --- */

	.section .scall.text,"ax",@progbits
system_call:
	pushm	r12		/* r12_orig */
	stmts	--sp, r0-lr
	zero_fp
	mfsr	r0, SYSREG_RAR_SUP
	mfsr	r1, SYSREG_RSR_SUP
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
	rcall   schedule_tail

	/* check for syscall tracing */
	get_thread_info r0
	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_ALLWORK_MASK, COH
	brne	syscall_exit_work
	rjmp    syscall_exit_cont

syscall_trace_enter:
	pushm	r8-r12
	rcall	syscall_trace
	popm	r8-r12
	rjmp	syscall_trace_cont

syscall_exit_work:
	bld	r1, TIF_SYSCALL_TRACE
	brcc	1f
	unmask_interrupts
	rcall	syscall_trace
	mask_interrupts
	ld.w	r1, r0[TI_flags]

1:	bld	r1, TIF_NEED_RESCHED
	brcc	2f
	unmask_interrupts
	rcall	schedule
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

2:	mov	r2, _TIF_SIGPENDING | _TIF_RESTORE_SIGMASK
	tst	r1, r2
	breq	3f
	unmask_interrupts
	mov	r12, sp
	mov	r11, r0
	rcall	do_notify_resume
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

3:	bld	r1, TIF_BREAKPOINT
	brcc	syscall_exit_cont
	mfsr	r3, SYSREG_TLBEHI
	lddsp	r2, sp[REG_PC]
	andl	r3, 0xff, COH
	lsl	r3, 1
	sbr	r3, 30
	sbr	r3, 0
	mtdr	DBGREG_BWA2A, r2
	mtdr	DBGREG_BWC2A, r3
	rjmp	syscall_exit_cont


	/* The slow path of the TLB miss handler */
page_table_not_present:
page_not_present:
	tlbmiss_restore
	sub	sp, 4
	stmts	--sp, r0-lr
	rcall	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	rcall	do_page_fault
	rjmp	ret_from_exception

	/* This function expects to find offending PC in SYSREG_RAR_EX */
save_full_context_ex:
	mfsr	r8, SYSREG_RSR_EX
	mov	r12, r8
	andh	r8, (MODE_MASK >> 16), COH
	mfsr	r11, SYSREG_RAR_EX
	brne	2f

1:	pushm	r11, r12	/* PC and SR */
	unmask_exceptions
	ret	r12

2:	sub	r10, sp, -(FRAME_SIZE_FULL - REG_LR)
	stdsp	sp[4], r10	/* replace saved SP */
	rjmp	1b

	/* Low-level exception handlers */
handle_critical:
	pushm	r12
	pushm	r0-r12
	rcall	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	rcall	do_critical_exception

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
	rcall	save_full_context_ex
	mov	r11, 1
	rjmp	1f

do_bus_error_read:
	sub	sp, 4
	stmts	--sp, r0-lr
	rcall	save_full_context_ex
	mov	r11, 0
1:	mfsr	r12, SYSREG_BEAR
	mov	r10, sp
	rcall	do_bus_error
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
	rcall	do_nmi
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
	rcall	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	rcall	do_address_exception
	rjmp	ret_from_exception

handle_protection_fault:
	sub	sp, 4
	stmts	--sp, r0-lr
	rcall	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	rcall	do_page_fault
	rjmp	ret_from_exception

	.align	1
do_illegal_opcode_ll:
	sub	sp, 4
	stmts	--sp, r0-lr
	rcall	save_full_context_ex
	mfsr	r12, SYSREG_ECR
	mov	r11, sp
	rcall	do_illegal_opcode
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
	rcall	save_full_context_ex
	unmask_interrupts
	mov	r12, 26
	mov	r11, sp
	rcall	do_fpe
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
	rcall	preempt_schedule_irq
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
	rcall	schedule
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	fault_exit_work

1:	mov	r2, _TIF_SIGPENDING | _TIF_RESTORE_SIGMASK
	tst	r1, r2
	breq	2f
	unmask_interrupts
	mov	r12, sp
	mov	r11, r0
	rcall	do_notify_resume
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	fault_exit_work

2:	bld	r1, TIF_BREAKPOINT
	brcc	fault_resume_user
	mfsr	r3, SYSREG_TLBEHI
	lddsp	r2, sp[REG_PC]
	andl	r3, 0xff, COH
	lsl	r3, 1
	sbr	r3, 30
	sbr	r3, 0
	mtdr	DBGREG_BWA2A, r2
	mtdr	DBGREG_BWC2A, r3
	rjmp	fault_resume_user

	/* If we get a debug trap from privileged context we end up here */
handle_debug_priv:
	/* Fix up LR and SP in regs. r11 contains the mode we came from */
	mfsr	r8, SYSREG_SR
	mov	r9, r8
	andh	r8, hi(~MODE_MASK)
	or	r8, r11
	mtsr	SYSREG_SR, r8
	sub	pc, -2
	stdsp	sp[REG_LR], lr
	mtsr	SYSREG_SR, r9
	sub	pc, -2
	sub	r10, sp, -FRAME_SIZE_FULL
	stdsp	sp[REG_SP], r10
	mov	r12, sp
	rcall	do_debug_priv

	/* Now, put everything back */
	ssrf	SR_EM_BIT
	popm	r10, r11
	mtsr	SYSREG_RAR_DBG, r10
	mtsr	SYSREG_RSR_DBG, r11
	mfsr	r8, SYSREG_SR
	mov	r9, r8
	andh	r8, hi(~MODE_MASK)
	andh	r11, hi(MODE_MASK)
	or	r8, r11
	mtsr	SYSREG_SR, r8
	sub	pc, -2
	popm	lr
	mtsr	SYSREG_SR, r9
	sub	pc, -2
	sub	sp, -4		/* skip SP */
	popm	r0-r12
	sub	sp, -4
	retd

	/*
	 * At this point, everything is masked, that is, interrupts,
	 * exceptions and debugging traps. We might get called from
	 * interrupt or exception context in some rare cases, but this
	 * will be taken care of by do_debug(), so we're not going to
	 * do a 100% correct context save here.
	 */
handle_debug:
	sub	sp, 4		/* r12_orig */
	stmts	--sp, r0-lr
	mfsr	r10, SYSREG_RAR_DBG
	mfsr	r11, SYSREG_RSR_DBG
	unmask_exceptions
	pushm	r10,r11
	andh	r11, (MODE_MASK >> 16), COH
	brne	handle_debug_priv

	mov	r12, sp
	rcall	do_debug

	lddsp	r10, sp[REG_SR]
	andh	r10, (MODE_MASK >> 16), COH
	breq	debug_resume_user

debug_restore_all:
	popm	r10,r11
	mask_exceptions
	mtsr	SYSREG_RSR_DBG, r11
	mtsr	SYSREG_RAR_DBG, r10
	ldmts	sp++, r0-lr
	sub	sp, -4
	retd

debug_resume_user:
	get_thread_info r0
	mask_interrupts

	ld.w	r1, r0[TI_flags]
	andl	r1, _TIF_DBGWORK_MASK, COH
	breq	debug_restore_all

1:	bld	r1, TIF_NEED_RESCHED
	brcc	2f
	unmask_interrupts
	rcall	schedule
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

2:	mov	r2, _TIF_SIGPENDING | _TIF_RESTORE_SIGMASK
	tst	r1, r2
	breq	3f
	unmask_interrupts
	mov	r12, sp
	mov	r11, r0
	rcall	do_notify_resume
	mask_interrupts
	ld.w	r1, r0[TI_flags]
	rjmp	1b

3:	bld	r1, TIF_SINGLE_STEP
	brcc	debug_restore_all
	mfdr	r2, DBGREG_DC
	sbr	r2, DC_SS_BIT
	mtdr	DBGREG_DC, r2
	rjmp	debug_restore_all

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
	pushm	r8-r9

	mov	r11, sp
	mov	r12, \level

	rcall	do_IRQ

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

1:	popm	r8-r9
	mtsr	rar_int\level, r8
	mtsr	rsr_int\level, r9
	ldmts	sp++,r0-lr
	sub	sp, -4		/* ignore r12_orig */
	rete

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
	rcall	preempt_schedule_irq
#endif
	rjmp	1b
	.endm

	.section .irq.text,"ax",@progbits

.global cpu_idle_sleep
cpu_idle_sleep:
	mask_interrupts
	get_thread_info r8
	ld.w	r9, r8[TI_flags]
	bld	r9, TIF_NEED_RESCHED
	brcs	cpu_idle_enable_int_and_exit
	sbr	r9, TIF_CPU_GOING_TO_SLEEP
	st.w	r8[TI_flags], r9
	unmask_interrupts
	sleep 0
cpu_idle_skip_sleep:
	mask_interrupts
	ld.w	r9, r8[TI_flags]
	cbr	r9, TIF_CPU_GOING_TO_SLEEP
	st.w	r8[TI_flags], r9
cpu_idle_enable_int_and_exit:
	unmask_interrupts
	retal	r12

	.global	irq_level0
	.global	irq_level1
	.global	irq_level2
	.global	irq_level3
	IRQ_LEVEL 0
	IRQ_LEVEL 1
	IRQ_LEVEL 2
	IRQ_LEVEL 3
