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
#include <asm/unwind.h>

#ifdef CONFIG_NEED_RET_TO_USER
#include <mach/entry-macro.S>
#else
	.macro  arch_ret_to_user, tmp1, tmp2
	.endm
#endif

#include "entry-header.S"


	.align	5
/*
 * This is the fast syscall return path.  We do as little as
 * possible here, and this includes saving r0 back into the SVC
 * stack.
 */
ret_fast_syscall:
 UNWIND(.fnstart	)
 UNWIND(.cantunwind	)
	disable_irq				@ disable interrupts
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	fast_work_pending
#if defined(CONFIG_IRQSOFF_TRACER)
	asm_trace_hardirqs_on
#endif

	/* perform architecture specific actions before user return */
	arch_ret_to_user r1, lr

	restore_user_regs fast = 1, offset = S_OFF
 UNWIND(.fnend		)

/*
 * Ok, we need to do extra processing, enter the slow path.
 */
fast_work_pending:
	str	r0, [sp, #S_R0+S_OFF]!		@ returned r0
work_pending:
	mov	r0, sp				@ 'regs'
	mov	r2, why				@ 'syscall'
	bl	do_work_pending
	cmp	r0, #0
	beq	no_work_pending
	movlt	scno, #(__NR_restart_syscall - __NR_SYSCALL_BASE)
	ldmia	sp, {r0 - r6}			@ have to reload r0 - r6
	b	local_restart			@ ... and off we go

/*
 * "slow" syscall return path.  "why" tells us if this was a real syscall.
 */
ENTRY(ret_to_user)
ret_slow_syscall:
	disable_irq				@ disable interrupts
ENTRY(ret_to_user_from_irq)
	ldr	r1, [tsk, #TI_FLAGS]
	tst	r1, #_TIF_WORK_MASK
	bne	work_pending
no_work_pending:
#if defined(CONFIG_IRQSOFF_TRACER)
	asm_trace_hardirqs_on
#endif
	/* perform architecture specific actions before user return */
	arch_ret_to_user r1, lr

	restore_user_regs fast = 0, offset = 0
ENDPROC(ret_to_user_from_irq)
ENDPROC(ret_to_user)

/*
 * This is how we return from a fork.
 */
ENTRY(ret_from_fork)
	bl	schedule_tail
	cmp	r5, #0
	movne	r0, r4
	movne	lr, pc
	movne	pc, r5
	get_thread_info tsk
	b	ret_slow_syscall
ENDPROC(ret_from_fork)

	.equ NR_syscalls,0
#define CALL(x) .equ NR_syscalls,NR_syscalls+1
#include "calls.S"

/*
 * Ensure that the system call table is equal to __NR_syscalls,
 * which is the value the rest of the system sees
 */
.ifne NR_syscalls - __NR_syscalls
.error "__NR_syscalls is not equal to the size of the syscall table"
.endif

#undef CALL
#define CALL(x) .long x

#ifdef CONFIG_FUNCTION_TRACER
/*
 * When compiling with -pg, gcc inserts a call to the mcount routine at the
 * start of every function.  In mcount, apart from the function's address (in
 * lr), we need to get hold of the function's caller's address.
 *
 * Older GCCs (pre-4.4) inserted a call to a routine called mcount like this:
 *
 *	bl	mcount
 *
 * These versions have the limitation that in order for the mcount routine to
 * be able to determine the function's caller's address, an APCS-style frame
 * pointer (which is set up with something like the code below) is required.
 *
 *	mov     ip, sp
 *	push    {fp, ip, lr, pc}
 *	sub     fp, ip, #4
 *
 * With EABI, these frame pointers are not available unless -mapcs-frame is
 * specified, and if building as Thumb-2, not even then.
 *
 * Newer GCCs (4.4+) solve this problem by introducing a new version of mcount,
 * with call sites like:
 *
 *	push	{lr}
 *	bl	__gnu_mcount_nc
 *
 * With these compilers, frame pointers are not necessary.
 *
 * mcount can be thought of as a function called in the middle of a subroutine
 * call.  As such, it needs to be transparent for both the caller and the
 * callee: the original lr needs to be restored when leaving mcount, and no
 * registers should be clobbered.  (In the __gnu_mcount_nc implementation, we
 * clobber the ip register.  This is OK because the ARM calling convention
 * allows it to be clobbered in subroutines and doesn't use it to hold
 * parameters.)
 *
 * When using dynamic ftrace, we patch out the mcount call by a "mov r0, r0"
 * for the mcount case, and a "pop {lr}" for the __gnu_mcount_nc case (see
 * arch/arm/kernel/ftrace.c).
 */

#ifndef CONFIG_OLD_MCOUNT
#if (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 4))
#error Ftrace requires CONFIG_FRAME_POINTER=y with GCC older than 4.4.0.
#endif
#endif

.macro mcount_adjust_addr rd, rn
	bic	\rd, \rn, #1		@ clear the Thumb bit if present
	sub	\rd, \rd, #MCOUNT_INSN_SIZE
.endm

.macro __mcount suffix
	mcount_enter
	ldr	r0, =ftrace_trace_function
	ldr	r2, [r0]
	adr	r0, .Lftrace_stub
	cmp	r0, r2
	bne	1f

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	ldr     r1, =ftrace_graph_return
	ldr     r2, [r1]
	cmp     r0, r2
	bne     ftrace_graph_caller\suffix

	ldr     r1, =ftrace_graph_entry
	ldr     r2, [r1]
	ldr     r0, =ftrace_graph_entry_stub
	cmp     r0, r2
	bne     ftrace_graph_caller\suffix
#endif

	mcount_exit

1: 	mcount_get_lr	r1			@ lr of instrumented func
	mcount_adjust_addr	r0, lr		@ instrumented function
	adr	lr, BSYM(2f)
	mov	pc, r2
2:	mcount_exit
.endm

.macro __ftrace_caller suffix
	mcount_enter

	mcount_get_lr	r1			@ lr of instrumented func
	mcount_adjust_addr	r0, lr		@ instrumented function

	.globl ftrace_call\suffix
ftrace_call\suffix:
	bl	ftrace_stub

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl ftrace_graph_call\suffix
ftrace_graph_call\suffix:
	mov	r0, r0
#endif

	mcount_exit
.endm

.macro __ftrace_graph_caller
	sub	r0, fp, #4		@ &lr of instrumented routine (&parent)
#ifdef CONFIG_DYNAMIC_FTRACE
	@ called from __ftrace_caller, saved in mcount_enter
	ldr	r1, [sp, #16]		@ instrumented routine (func)
	mcount_adjust_addr	r1, r1
#else
	@ called from __mcount, untouched in lr
	mcount_adjust_addr	r1, lr	@ instrumented routine (func)
#endif
	mov	r2, fp			@ frame pointer
	bl	prepare_ftrace_return
	mcount_exit
.endm

#ifdef CONFIG_OLD_MCOUNT
/*
 * mcount
 */

.macro mcount_enter
	stmdb	sp!, {r0-r3, lr}
.endm

.macro mcount_get_lr reg
	ldr	\reg, [fp, #-4]
.endm

.macro mcount_exit
	ldr	lr, [fp, #-4]
	ldmia	sp!, {r0-r3, pc}
.endm

ENTRY(mcount)
#ifdef CONFIG_DYNAMIC_FTRACE
	stmdb	sp!, {lr}
	ldr	lr, [fp, #-4]
	ldmia	sp!, {pc}
#else
	__mcount _old
#endif
ENDPROC(mcount)

#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(ftrace_caller_old)
	__ftrace_caller _old
ENDPROC(ftrace_caller_old)
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller_old)
	__ftrace_graph_caller
ENDPROC(ftrace_graph_caller_old)
#endif

.purgem mcount_enter
.purgem mcount_get_lr
.purgem mcount_exit
#endif

/*
 * __gnu_mcount_nc
 */

.macro mcount_enter
	stmdb	sp!, {r0-r3, lr}
.endm

.macro mcount_get_lr reg
	ldr	\reg, [sp, #20]
.endm

.macro mcount_exit
	ldmia	sp!, {r0-r3, ip, lr}
	mov	pc, ip
.endm

ENTRY(__gnu_mcount_nc)
#ifdef CONFIG_DYNAMIC_FTRACE
	mov	ip, lr
	ldmia	sp!, {lr}
	mov	pc, ip
#else
	__mcount
#endif
ENDPROC(__gnu_mcount_nc)

#ifdef CONFIG_DYNAMIC_FTRACE
ENTRY(ftrace_caller)
	__ftrace_caller
ENDPROC(ftrace_caller)
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	__ftrace_graph_caller
ENDPROC(ftrace_graph_caller)
#endif

.purgem mcount_enter
.purgem mcount_get_lr
.purgem mcount_exit

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	.globl return_to_handler
return_to_handler:
	stmdb	sp!, {r0-r3}
	mov	r0, fp			@ frame pointer
	bl	ftrace_return_to_handler
	mov	lr, r0			@ r0 has real ret addr
	ldmia	sp!, {r0-r3}
	mov	pc, lr
#endif

ENTRY(ftrace_stub)
.Lftrace_stub:
	mov	pc, lr
ENDPROC(ftrace_stub)

#endif /* CONFIG_FUNCTION_TRACER */

/*=============================================================================
 * SWI handler
 *-----------------------------------------------------------------------------
 */

	.align	5
ENTRY(vector_swi)
	sub	sp, sp, #S_FRAME_SIZE
	stmia	sp, {r0 - r12}			@ Calling r0 - r12
 ARM(	add	r8, sp, #S_PC		)
 ARM(	stmdb	r8, {sp, lr}^		)	@ Calling sp, lr
 THUMB(	mov	r8, sp			)
 THUMB(	store_user_sp_lr r8, r10, S_SP	)	@ calling sp, lr
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
#endif
#ifdef CONFIG_CPU_ENDIAN_BE8
	rev	r10, r10			@ little endian instruction
#endif

#elif defined(CONFIG_AEABI)

	/*
	 * Pure EABI user space always put syscall number into scno (r7).
	 */
#elif defined(CONFIG_ARM_THUMB)
	/* Legacy ABI only, possibly thumb mode. */
	tst	r8, #PSR_T_BIT			@ this is SPSR from save_user_regs
	addne	scno, r7, #__NR_SYSCALL_BASE	@ put OS number in
	ldreq	scno, [lr, #-4]

#else
	/* Legacy ABI only. */
	ldr	scno, [lr, #-4]			@ get SWI instruction
#endif

#ifdef CONFIG_ALIGNMENT_TRAP
	ldr	ip, __cr_alignment
	ldr	ip, [ip]
	mcr	p15, 0, ip, c1, c0		@ update control register
#endif
	enable_irq

	get_thread_info tsk
	adr	tbl, sys_call_table		@ load syscall table pointer

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

local_restart:
	ldr	r10, [tsk, #TI_FLAGS]		@ check for syscall tracing
	stmdb	sp!, {r4, r5}			@ push fifth and sixth args

#ifdef CONFIG_SECCOMP
	tst	r10, #_TIF_SECCOMP
	beq	1f
	mov	r0, scno
	bl	__secure_computing	
	add	r0, sp, #S_R0 + S_OFF		@ pointer to regs
	ldmia	r0, {r0 - r3}			@ have to reload r0 - r3
1:
#endif

	tst	r10, #_TIF_SYSCALL_WORK		@ are we tracing syscalls?
	bne	__sys_trace

	cmp	scno, #NR_syscalls		@ check upper syscall limit
	adr	lr, BSYM(ret_fast_syscall)	@ return address
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
	mov	r1, scno
	add	r0, sp, #S_OFF
	bl	syscall_trace_enter

	adr	lr, BSYM(__sys_trace_return)	@ return address
	mov	scno, r0			@ syscall number (possibly new)
	add	r1, sp, #S_R0 + S_OFF		@ pointer to regs
	cmp	scno, #NR_syscalls		@ check upper syscall limit
	ldmccia	r1, {r0 - r6}			@ have to reload r0 - r6
	stmccia	sp, {r4, r5}			@ and update the stack args
	ldrcc	pc, [tbl, scno, lsl #2]		@ call sys_* routine
	b	2b

__sys_trace_return:
	str	r0, [sp, #S_R0 + S_OFF]!	@ save returned r0
	mov	r1, scno
	mov	r0, sp
	bl	syscall_trace_exit
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

sys_clone_wrapper:
		add	ip, sp, #S_OFF
		str	ip, [sp, #4]
		b	sys_clone
ENDPROC(sys_clone_wrapper)

sys_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
		b	sys_sigreturn
ENDPROC(sys_sigreturn_wrapper)

sys_rt_sigreturn_wrapper:
		add	r0, sp, #S_OFF
		mov	why, #0		@ prevent syscall restart handling
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
		beq	sys_mmap_pgoff
		mov	r0, #-EINVAL
		mov	pc, lr
#else
		str	r5, [sp, #4]
		b	sys_mmap_pgoff
#endif
ENDPROC(sys_mmap2)

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

