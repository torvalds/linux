/*
 *  linux/arch/arm/kernel/entry-common.S
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/unistd.h>
#include <asm/ftrace.h>
#include <mach/entry-macro.S>

#include "entry-header.S"


	.align	5
/*
 * This is the fast syscall return path.  We do as little as
 * possible here, and this includes saving r0 back into the SVC
 * stack.
 */
ret_fast_syscall:
	disable_irq				@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	fast_work_pending

	/* perform architecture specific actions before user return */
	arch_ret_to_user r1, lr

	@ fast_restore_user_regs
	ldr	r1, [sp, #S_OFF + S_PSR]	@ get calling cpsr
	ldr	lr, [sp, #S_OFF + S_PC]!	@ get pc
	msr	spsr_cxsf, r1			@ save in spsr_svc
	ldmdb	sp, {r1 - lr}^			@ get calling r1 - lr
	mov	r0, r0
	add	sp, sp, #S_FRAME_SIZE - S_PC
	movs	pc, lr				@ return & move spsr_svc into cpsr

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
fast_work_pending:
	str	r0, [sp, #S_R0+S_OFF]!		@ returned r0
work_pending:
	tst	r1, #_TIF_NEED_RESCHED
	bne	work_resched
	tst	r1, #_TIF_SIGPENDING
	beq	no_work_pending
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	bl	do_notify_resume
	b	ret_slow_syscall		@ Check work again

work_resched:
	bl	schedule
/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
	disable_irq				@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	work_pending
no_work_pending:
	/* perform architecture specific actions before user return */
	arch_ret_to_user r1, lr

	@ slow_restore_user_regs
	ldr	r1, [sp, #S_PSR]		@ get calling cpsr
	ldr	lr, [sp, #S_PC]!		@ get pc
	msr	spsr_cxsf, r1			@ save in spsr_svc
	ldmdb	sp, {r0 - lr}^			@ get calling r0 - lr
	mov	r0, r0
	add	sp, sp, #S_FRAME_SIZE - S_PC
	movs	pc, lr				@ return & move spsr_svc into cpsr
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	get_thread_info tsk
	ldr	r1, [tsk, #TI_FLAGS]		@ check for syscall tracing
	mov	why, #1
	tst	r1, #_TIF_SYSCALL_TRACE		@ are we tracing syscalls?
	beq	ret_slow_syscall
	mov	r1, sp
	mov	r0, #1				@ trace exit [IP = 1]
	bl	syscall_trace
	b	ret_slow_syscall
ENDPROC(ret_from_fork)

	.equ NR_syscalls,0
#define CALL(x) .equ NR_syscalls,NR_syscalls+1
#include "calls.S"
#undef CALL
#define CALL(x) .long x

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(mcount)
	stmdb sp!, {r0-r3, lr}
	mov r0, lr
	sub r0, r0, #MCOUNT_INSN_SIZE

	.globl mcount_call
mcount_call:
	bl ftrace_stub
	ldr lr, [fp, #-4]			@ restore lr
	ldmia sp!, {r0-r3, pc}

ENTRY(ftrace_caller)
	stmdb sp!, {r0-r3, lr}
	ldr r1, [fp, #-4]
	mov r0, lr
	sub r0, r0, #MCOUNT_INSN_SIZE

	.globl ftrace_call
ftrace_call:
	bl ftrace_stub
	ldr lr, [fp, #-4]			@ restore lr
	ldmia sp!, {r0-r3, pc}

#else

ENTRY(mcount)
	stmdb sp!, {r0-r3, lr}
	ldr r0, =ftrace_trace_function
	ldr r2, [r0]
	adr r0, ftrace_stub
	cmp r0, r2
	bne trace
	ldr lr, [fp, #-4]			@ restore lr
	ldmia sp!, {r0-r3, pc}

trace:
	ldr r1, [fp, #-4]			@ lr of instrumented routine
	mov r0, lr
	sub r0, r0, #MCOUNT_INSN_SIZE
	mov lr, pc
	mov pc, r2
	mov lr, r1				@ restore lr
	ldmia sp!, {r0-r3, pc}

#endif /* CONFIG_DYNAMIC_FTRACE */

	.globl ftrace_stub
ftrace_stub:
	mov pc, lr

#endif /* CONFIG_FUNCTION_TRACER */

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

	/* If we're optimising for StrongARM the resulting code won't 
	   run on an ARM7 and we can save a couple of instructions.  
								--pb */
#ifdef CONFIG_CPU_ARM710
#define A710(code...) code
.Larm710bug:
	ldmia	sp, {r0 - lr}^			@ Get calling r0 - lr
	mov	r0, r0
	add	sp, sp, #S_FRAME_SIZE
	subs	pc, lr, #4
#else
#define A710(code...)
#endif

	.align	5
ENTRY(vector_swi)
	sub	sp, sp, #S_FRAME_SIZE
	stmia	sp, {r0 - r12}			@ Calling r0 - r12
	add	r8, sp, #S_PC
	stmdb	r8, {sp, lr}^			@ Calling sp, lr
	mrs	r8, spsr			@ called from non-FIQ mode, so ok.
	str	lr, [sp, #S_PC]			@ Save calling PC
	str	r8, [sp, #S_PSR]		@ Save CPSR
	str	r0, [sp, #S_OLD_R0]		@ Save OLD_R0
	zero_fp

	/*
	 * Get the system call number.
	 */

#if defined(CONFIG_OABI_COMPAT)

	/*
	 * If we have CONFIG_OABI_COMPAT then we need to look at the swi
	 * value to determine if it is an EABI or an old ABI call.
	 */
#ifdef CONFIG_ARM_THUMB
	tst	r8, #PSR_T_BIT
	movne	r10, #0				@ no thumb OABI emulation
	ldreq	r10, [lr, #-4]			@ get SWI instruction
#else
	ldr	r10, [lr, #-4]			@ get SWI instruction
  A710(	and	ip, r10, #0x0f000000		@ check for SWI		)
  A710(	teq	ip, #0x0f000000						)
  A710(	bne	.Larm710bug						)
#endif

#elif defined(CONFIG_AEABI)

	/*
	 * Pure EABI user space always put syscall number into scno (r7).
	 */
  A710(	ldr	ip, [lr, #-4]			@ get SWI instruction	)
  A710(	and	ip, ip, #0x0f000000		@ check for SWI		)
  A710(	teq	ip, #0x0f000000						)
  A710(	bne	.Larm710bug						)

#elif defined(CONFIG_ARM_THUMB)

	/* Legacy ABI only, possibly thumb mode. */
	tst	r8, #PSR_T_BIT			@ this is SPSR from save_user_regs
	addne	scno, r7, #__NR_SYSCALL_BASE	@ put OS number in
	ldreq	scno, [lr, #-4]

#else

	/* Legacy ABI only. */
	ldr	scno, [lr, #-4]			@ get SWI instruction
  A710(	and	ip, scno, #0x0f000000		@ check for SWI		)
  A710(	teq	ip, #0x0f000000						)
  A710(	bne	.Larm710bug						)

#endif

#ifdef CONFIG_ALIGNMENT_TRAP
	ldr	ip, __cr_alignment
	ldr	ip, [ip]
	mcr	p15, 0, ip, c1, c0		@ update control register
#endif
	enable_irq

	get_thread_info tsk
	adr	tbl, sys_call_table		@ load syscall table pointer
	ldr	ip, [tsk, #TI_FLAGS]		@ check for syscall tracing

#if defined(CONFIG_OABI_COMPAT)
	/*
	 * If the swi argument is zero, this is an EABI call and we do nothing.
	 *
	 * If this is an old ABI call, get the syscall number into scno and
	 * get the old ABI syscall table address.
	 */
	bics	r10, r10, #0xff000000
	eorne	scno, r10, #__NR_OABI_SYSCALL_BASE
	ldrne	tbl, =sys_oabi_call_table
#elif !defined(CONFIG_AEABI)
	bic	scno, scno, #0xff000000		@ mask off SWI op-code
	eor	scno, scno, #__NR_SYSCALL_BASE	@ check OS number
#endif

	stmdb	sp!, {r4, r5}			@ push fifth and sixth args
	tst	ip, #_TIF_SYSCALL_TRACE		@ are we tracing syscalls?
	bne	__sys_trace

	cmp	scno, #NR_syscalls		@ check upper syscall limit
	adr	lr, ret_fast_syscall		@ return address
	ldrcc	pc, [tbl, scno, lsl #2]		@ call sys_* routine

	add	r1, sp, #S_OFF
2:	mov	why, #0				@ no longer a real syscall
	cmp	scno, #(__ARM_NR_BASE - __NR_SYSCALL_BASE)
	eor	r0, scno, #__NR_SYSCALL_BASE	@ put OS number back
	bcs	arm_syscall	
	b	sys_ni_syscall			@ not private func
ENDPROC(vector_swi)

	/*
	 * This is the really slow path.  We're going to be doing
	 * context switches, and waiting for our parent to respond.
	 */
__sys_trace:
	mov	r2, scno
	add	r1, sp, #S_OFF
	mov	r0, #0				@ trace entry [IP = 0]
	bl	syscall_trace

	adr	lr, __sys_trace_return		@ return address
	mov	scno, r0			@ syscall number (possibly new)
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	cmp	scno, #NR_syscalls		@ check upper syscall limit
	ldmccia	r1, {r0 - r3}			@ have to reload r0 - r3
	ldrcc	pc, [tbl, scno, lsl #2]		@ call sys_* routine
	b	2b

__sys_trace_return:
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
	mov	r2, scno
	mov	r1, sp
	mov	r0, #1				@ trace exit [IP = 1]
	bl	syscall_trace
	b	ret_slow_syscall

	.align	5
#ifdef CONFIG_ALIGNMENT_TRAP
	.type	__cr_alignment, #object
__cr_alignment:
	.word	cr_alignment
#endif
	.ltorg

/*
 * This is the syscall table declaration for native ABI syscalls.
 * With EABI a couple syscalls are obsolete and defined as sys_ni_syscall.
 */
#define ABI(native, compat) native
#ifdef CONFIG_AEABI
#define OBSOLETE(syscall) sys_ni_syscall
#else
#define OBSOLETE(syscall) syscall
#endif

	.type	sys_call_table, #object
ENTRY(sys_call_table)
#include "calls.S"
#undef ABI
#undef OBSOLETE

/*============================================================================
 * Special system call wrappers
 */
@ r0 = syscall number
@ r8 = syscall table
sys_syscall:
		bic	scno, r0, #__NR_OABI_SYSCALL_BASE
		cmp	scno, #__NR_syscall - __NR_SYSCALL_BASE
		cmpne	scno, #NR_syscalls	@ check range
		stmloia	sp, {r5, r6}		@ shuffle args
		movlo	r0, r1
		movlo	r1, r2
		movlo	r2, r3
		movlo	r3, r4
		ldrlo	pc, [tbl, scno, lsl #2]
		b	sys_ni_syscall
ENDPROC(sys_syscall)

sys_fork_wrapper:
		add	r0, sp, #S_OFF
		b	sys_fork
ENDPROC(sys_fork_wrapper)

sys_vfork_wrapper:
		add	r0, sp, #S_OFF
		b	sys_vfork
ENDPROC(sys_vfork_wrapper)

sys_execve_wrapper:
		add	r3, sp, #S_OFF
		b	sys_execve
ENDPROC(sys_execve_wrapper)

sys_clone_wrapper:
		add	ip, sp, #S_OFF
		str	ip, [sp, #4]
		b	sys_clone
ENDPROC(sys_clone_wrapper)

sys_sigsuspend_wrapper:
		add	r3, sp, #S_OFF
		b	sys_sigsuspend
ENDPROC(sys_sigsuspend_wrapper)

sys_rt_sigsuspend_wrapper:
		add	r2, sp, #S_OFF
		b	sys_rt_sigsuspend
ENDPROC(sys_rt_sigsuspend_wrapper)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	sys_sigreturn
ENDPROC(sys_sigreturn_wrapper)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		b	sys_rt_sigreturn
ENDPROC(sys_rt_sigreturn_wrapper)

sys_sigaltstack_wrapper:
		ldr	r2, [sp, #S_OFF + S_SP]
		b	do_sigaltstack
ENDPROC(sys_sigaltstack_wrapper)

sys_statfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_statfs64
ENDPROC(sys_statfs64_wrapper)

sys_fstatfs64_wrapper:
		teq	r1, #88
		moveq	r1, #84
		b	sys_fstatfs64
ENDPROC(sys_fstatfs64_wrapper)

/*
 * Note: off_4k (r5) is always units of 4K.  If we can't do the requested
 * offset, we return EINVAL.
 */
sys_mmap2:
#if PAGE_SHIFT > 12
		tst	r5, #PGOFF_MASK
		moveq	r5, r5, lsr #PAGE_SHIFT - 12
		streq	r5, [sp, #4]
		beq	do_mmap2
		mov	r0, #-EINVAL
		mov	pc, lr
#else
		str	r5, [sp, #4]
		b	do_mmap2
#endif
ENDPROC(sys_mmap2)

ENTRY(pabort_ifar)
		mrc	p15, 0, r0, cr6, cr0, 2
ENTRY(pabort_noifar)
		mov	pc, lr
ENDPROC(pabort_ifar)
ENDPROC(pabort_noifar)

#ifdef CONFIG_OABI_COMPAT

/*
 * These are syscalls with argument register differences
 */

sys_oabi_pread64:
		stmia	sp, {r3, r4}
		b	sys_pread64
ENDPROC(sys_oabi_pread64)

sys_oabi_pwrite64:
		stmia	sp, {r3, r4}
		b	sys_pwrite64
ENDPROC(sys_oabi_pwrite64)

sys_oabi_truncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_truncate64
ENDPROC(sys_oabi_truncate64)

sys_oabi_ftruncate64:
		mov	r3, r2
		mov	r2, r1
		b	sys_ftruncate64
ENDPROC(sys_oabi_ftruncate64)

sys_oabi_readahead:
		str	r3, [sp]
		mov	r3, r2
		mov	r2, r1
		b	sys_readahead
ENDPROC(sys_oabi_readahead)

/*
 * Let's declare a second syscall table for old ABI binaries
 * using the compatibility syscall entries.
 */
#define ABI(native, compat) compat
#define OBSOLETE(syscall) syscall

	.type	sys_oabi_call_table, #object
ENTRY(sys_oabi_call_table)
#include "calls.S"
#undef ABI
#undef OBSOLETE

#endif

