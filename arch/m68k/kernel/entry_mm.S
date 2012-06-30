/* -*- mode: asm -*-
 *
 *  linux/arch/m68k/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 *
 */

/*
 * entry.S  contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 */

/*
 * 12/03/96 Jes: Currently we only support m68k single-cpu systems, so
 *               all pointers that used to be 'current' are now entry
 *               number 0 in the 'current_set' list.
 *
 *  6/05/00 RZ:	 addedd writeback completion after return from sighandler
 *		 for 68040
 */

#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/traps.h>
#include <asm/unistd.h>

#include <asm/asm-offsets.h>

.globl system_call, buserr, trap, resume
.globl sys_call_table
.globl sys_fork, sys_clone, sys_vfork
.globl ret_from_interrupt, bad_interrupt
.globl auto_irqhandler_fixup
.globl user_irqvec_fixup

.text
ENTRY(buserr)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	buserr_c
	addql	#4,%sp
	jra	.Lret_from_exception

ENTRY(trap)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%sp,%sp@-		| stack frame pointer argument
	bsrl	trap_c
	addql	#4,%sp
	jra	.Lret_from_exception

	| After a fork we jump here directly from resume,
	| so that %d1 contains the previous task
	| schedule_tail now used regardless of CONFIG_SMP
ENTRY(ret_from_fork)
	movel	%d1,%sp@-
	jsr	schedule_tail
	addql	#4,%sp
	jra	.Lret_from_exception

do_trace_entry:
	movel	#-ENOSYS,%sp@(PT_OFF_D0)| needed for strace
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	movel	%sp@(PT_OFF_ORIG_D0),%d0
	cmpl	#NR_syscalls,%d0
	jcs	syscall
badsys:
	movel	#-ENOSYS,%sp@(PT_OFF_D0)
	jra	ret_from_syscall

do_trace_exit:
	subql	#4,%sp
	SAVE_SWITCH_STACK
	jbsr	syscall_trace
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jra	.Lret_from_exception

ENTRY(ret_from_signal)
	movel	%curptr@(TASK_STACK),%a1
	tstb	%a1@(TINFO_FLAGS+2)
	jge	1f
	jbsr	syscall_trace
1:	RESTORE_SWITCH_STACK
	addql	#4,%sp
/* on 68040 complete pending writebacks if any */
#ifdef CONFIG_M68040
	bfextu	%sp@(PT_OFF_FORMATVEC){#0,#4},%d0
	subql	#7,%d0				| bus error frame ?
	jbne	1f
	movel	%sp,%sp@-
	jbsr	berr_040cleanup
	addql	#4,%sp
1:
#endif
	jra	.Lret_from_exception

ENTRY(system_call)
	SAVE_ALL_SYS

	GET_CURRENT(%d1)
	movel	%d1,%a1

	| save top of frame
	movel	%sp,%curptr@(TASK_THREAD+THREAD_ESP0)

	| syscall trace?
	tstb	%a1@(TINFO_FLAGS+2)
	jmi	do_trace_entry
	cmpl	#NR_syscalls,%d0
	jcc	badsys
syscall:
	jbsr	@(sys_call_table,%d0:l:4)@(0)
	movel	%d0,%sp@(PT_OFF_D0)	| save the return value
ret_from_syscall:
	|oriw	#0x0700,%sr
	movel	%curptr@(TASK_STACK),%a1
	movew	%a1@(TINFO_FLAGS+2),%d0
	jne	syscall_exit_work
1:	RESTORE_ALL

syscall_exit_work:
	btst	#5,%sp@(PT_OFF_SR)	| check if returning to kernel
	bnes	1b			| if so, skip resched, signals
	lslw	#1,%d0
	jcs	do_trace_exit
	jmi	do_delayed_trace
	lslw	#8,%d0
	jne	do_signal_return
	pea	resume_userspace
	jra	schedule


ENTRY(ret_from_exception)
.Lret_from_exception:
	btst	#5,%sp@(PT_OFF_SR)	| check if returning to kernel
	bnes	1f			| if so, skip resched, signals
	| only allow interrupts when we are really the last one on the
	| kernel stack, otherwise stack overflow can occur during
	| heavy interrupt load
	andw	#ALLOWINT,%sr

resume_userspace:
	movel	%curptr@(TASK_STACK),%a1
	moveb	%a1@(TINFO_FLAGS+3),%d0
	jne	exit_work
1:	RESTORE_ALL

exit_work:
	| save top of frame
	movel	%sp,%curptr@(TASK_THREAD+THREAD_ESP0)
	lslb	#1,%d0
	jne	do_signal_return
	pea	resume_userspace
	jra	schedule


do_signal_return:
	|andw	#ALLOWINT,%sr
	subql	#4,%sp			| dummy return address
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	bsrl	do_notify_resume
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	addql	#4,%sp
	jbra	resume_userspace

do_delayed_trace:
	bclr	#7,%sp@(PT_OFF_SR)	| clear trace bit in SR
	pea	1			| send SIGTRAP
	movel	%curptr,%sp@-
	pea	LSIGTRAP
	jbsr	send_sig
	addql	#8,%sp
	addql	#4,%sp
	jbra	resume_userspace


/* This is the main interrupt handler for autovector interrupts */

ENTRY(auto_inthandler)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%d0,%a1
	addqb	#1,%a1@(TINFO_PREEMPT+1)
					|  put exception # in d0
	bfextu	%sp@(PT_OFF_FORMATVEC){#4,#10},%d0
	subw	#VEC_SPUR,%d0

	movel	%sp,%sp@-
	movel	%d0,%sp@-		|  put vector # on stack
auto_irqhandler_fixup = . + 2
	jsr	do_IRQ			|  process the IRQ
	addql	#8,%sp			|  pop parameters off stack

ret_from_interrupt:
	movel	%curptr@(TASK_STACK),%a1
	subqb	#1,%a1@(TINFO_PREEMPT+1)
	jeq	ret_from_last_interrupt
2:	RESTORE_ALL

	ALIGN
ret_from_last_interrupt:
	moveq	#(~ALLOWINT>>8)&0xff,%d0
	andb	%sp@(PT_OFF_SR),%d0
	jne	2b

	/* check if we need to do software interrupts */
	tstl	irq_stat+CPUSTAT_SOFTIRQ_PENDING
	jeq	.Lret_from_exception
	pea	ret_from_exception
	jra	do_softirq

/* Handler for user defined interrupt vectors */

ENTRY(user_inthandler)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%d0,%a1
	addqb	#1,%a1@(TINFO_PREEMPT+1)
					|  put exception # in d0
	bfextu	%sp@(PT_OFF_FORMATVEC){#4,#10},%d0
user_irqvec_fixup = . + 2
	subw	#VEC_USER,%d0

	movel	%sp,%sp@-
	movel	%d0,%sp@-		|  put vector # on stack
	jsr	do_IRQ			|  process the IRQ
	addql	#8,%sp			|  pop parameters off stack

	movel	%curptr@(TASK_STACK),%a1
	subqb	#1,%a1@(TINFO_PREEMPT+1)
	jeq	ret_from_last_interrupt
	RESTORE_ALL

/* Handler for uninitialized and spurious interrupts */

ENTRY(bad_inthandler)
	SAVE_ALL_INT
	GET_CURRENT(%d0)
	movel	%d0,%a1
	addqb	#1,%a1@(TINFO_PREEMPT+1)

	movel	%sp,%sp@-
	jsr	handle_badint
	addql	#4,%sp

	movel	%curptr@(TASK_STACK),%a1
	subqb	#1,%a1@(TINFO_PREEMPT+1)
	jeq	ret_from_last_interrupt
	RESTORE_ALL


ENTRY(sys_fork)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_fork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_clone)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_clone
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_vfork)
	SAVE_SWITCH_STACK
	pea	%sp@(SWITCH_STACK_SIZE)
	jbsr	m68k_vfork
	addql	#4,%sp
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_sigreturn
	RESTORE_SWITCH_STACK
	rts

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	jbsr	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	rts

resume:
	/*
	 * Beware - when entering resume, prev (the current task) is
	 * in a0, next (the new task) is in a1,so don't change these
	 * registers until their contents are no longer needed.
	 */

	/* save sr */
	movew	%sr,%a0@(TASK_THREAD+THREAD_SR)

	/* save fs (sfc,%dfc) (may be pointing to kernel memory) */
	movec	%sfc,%d0
	movew	%d0,%a0@(TASK_THREAD+THREAD_FS)

	/* save usp */
	/* it is better to use a movel here instead of a movew 8*) */
	movec	%usp,%d0
	movel	%d0,%a0@(TASK_THREAD+THREAD_USP)

	/* save non-scratch registers on stack */
	SAVE_SWITCH_STACK

	/* save current kernel stack pointer */
	movel	%sp,%a0@(TASK_THREAD+THREAD_KSP)

	/* save floating point context */
#ifndef CONFIG_M68KFPU_EMU_ONLY
#ifdef CONFIG_M68KFPU_EMU
	tstl	m68k_fputype
	jeq	3f
#endif
	fsave	%a0@(TASK_THREAD+THREAD_FPSTATE)

#if defined(CONFIG_M68060)
#if !defined(CPU_M68060_ONLY)
	btst	#3,m68k_cputype+3
	beqs	1f
#endif
	/* The 060 FPU keeps status in bits 15-8 of the first longword */
	tstb	%a0@(TASK_THREAD+THREAD_FPSTATE+2)
	jeq	3f
#if !defined(CPU_M68060_ONLY)
	jra	2f
#endif
#endif /* CONFIG_M68060 */
#if !defined(CPU_M68060_ONLY)
1:	tstb	%a0@(TASK_THREAD+THREAD_FPSTATE)
	jeq	3f
#endif
2:	fmovemx	%fp0-%fp7,%a0@(TASK_THREAD+THREAD_FPREG)
	fmoveml	%fpcr/%fpsr/%fpiar,%a0@(TASK_THREAD+THREAD_FPCNTL)
3:
#endif	/* CONFIG_M68KFPU_EMU_ONLY */
	/* Return previous task in %d1 */
	movel	%curptr,%d1

	/* switch to new task (a1 contains new task) */
	movel	%a1,%curptr

	/* restore floating point context */
#ifndef CONFIG_M68KFPU_EMU_ONLY
#ifdef CONFIG_M68KFPU_EMU
	tstl	m68k_fputype
	jeq	4f
#endif
#if defined(CONFIG_M68060)
#if !defined(CPU_M68060_ONLY)
	btst	#3,m68k_cputype+3
	beqs	1f
#endif
	/* The 060 FPU keeps status in bits 15-8 of the first longword */
	tstb	%a1@(TASK_THREAD+THREAD_FPSTATE+2)
	jeq	3f
#if !defined(CPU_M68060_ONLY)
	jra	2f
#endif
#endif /* CONFIG_M68060 */
#if !defined(CPU_M68060_ONLY)
1:	tstb	%a1@(TASK_THREAD+THREAD_FPSTATE)
	jeq	3f
#endif
2:	fmovemx	%a1@(TASK_THREAD+THREAD_FPREG),%fp0-%fp7
	fmoveml	%a1@(TASK_THREAD+THREAD_FPCNTL),%fpcr/%fpsr/%fpiar
3:	frestore %a1@(TASK_THREAD+THREAD_FPSTATE)
4:
#endif	/* CONFIG_M68KFPU_EMU_ONLY */

	/* restore the kernel stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_KSP),%sp

	/* restore non-scratch registers */
	RESTORE_SWITCH_STACK

	/* restore user stack pointer */
	movel	%a1@(TASK_THREAD+THREAD_USP),%a0
	movel	%a0,%usp

	/* restore fs (sfc,%dfc) */
	movew	%a1@(TASK_THREAD+THREAD_FS),%a0
	movec	%a0,%sfc
	movec	%a0,%dfc

	/* restore status register */
	movew	%a1@(TASK_THREAD+THREAD_SR),%sr

	rts

