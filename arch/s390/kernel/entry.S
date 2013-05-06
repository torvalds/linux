/*
 *    S390 low-level entry points.
 *
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Hartmut Penner (hp@de.ibm.com),
 *		 Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <asm/sigp.h>

__PT_R0      =	__PT_GPRS
__PT_R1      =	__PT_GPRS + 4
__PT_R2      =	__PT_GPRS + 8
__PT_R3      =	__PT_GPRS + 12
__PT_R4      =	__PT_GPRS + 16
__PT_R5      =	__PT_GPRS + 20
__PT_R6      =	__PT_GPRS + 24
__PT_R7      =	__PT_GPRS + 28
__PT_R8      =	__PT_GPRS + 32
__PT_R9      =	__PT_GPRS + 36
__PT_R10     =	__PT_GPRS + 40
__PT_R11     =	__PT_GPRS + 44
__PT_R12     =	__PT_GPRS + 48
__PT_R13     =	__PT_GPRS + 524
__PT_R14     =	__PT_GPRS + 56
__PT_R15     =	__PT_GPRS + 60

_TIF_WORK_SVC = (_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_NEED_RESCHED | \
		 _TIF_MCCK_PENDING | _TIF_PER_TRAP )
_TIF_WORK_INT = (_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_NEED_RESCHED | \
		 _TIF_MCCK_PENDING)
_TIF_TRACE    = (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | _TIF_SECCOMP | \
		 _TIF_SYSCALL_TRACEPOINT)

STACK_SHIFT = PAGE_SHIFT + THREAD_ORDER
STACK_SIZE  = 1 << STACK_SHIFT
STACK_INIT  = STACK_SIZE - STACK_FRAME_OVERHEAD - __PT_SIZE

#define BASED(name) name-system_call(%r13)

	.macro	TRACE_IRQS_ON
#ifdef CONFIG_TRACE_IRQFLAGS
	basr	%r2,%r0
	l	%r1,BASED(.Lhardirqs_on)
	basr	%r14,%r1		# call trace_hardirqs_on_caller
#endif
	.endm

	.macro	TRACE_IRQS_OFF
#ifdef CONFIG_TRACE_IRQFLAGS
	basr	%r2,%r0
	l	%r1,BASED(.Lhardirqs_off)
	basr	%r14,%r1		# call trace_hardirqs_off_caller
#endif
	.endm

	.macro	LOCKDEP_SYS_EXIT
#ifdef CONFIG_LOCKDEP
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jz	.+10
	l	%r1,BASED(.Llockdep_sys_exit)
	basr	%r14,%r1		# call lockdep_sys_exit
#endif
	.endm

	.macro	CHECK_STACK stacksize,savearea
#ifdef CONFIG_CHECK_STACK
	tml	%r15,\stacksize - CONFIG_STACK_GUARD
	la	%r14,\savearea
	jz	stack_overflow
#endif
	.endm

	.macro	SWITCH_ASYNC savearea,stack,shift
	tmh	%r8,0x0001		# interrupting from user ?
	jnz	1f
	lr	%r14,%r9
	sl	%r14,BASED(.Lcritical_start)
	cl	%r14,BASED(.Lcritical_length)
	jhe	0f
	la	%r11,\savearea		# inside critical section, do cleanup
	bras	%r14,cleanup_critical
	tmh	%r8,0x0001		# retest problem state after cleanup
	jnz	1f
0:	l	%r14,\stack		# are we already on the target stack?
	slr	%r14,%r15
	sra	%r14,\shift
	jnz	1f
	CHECK_STACK 1<<\shift,\savearea
	ahi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	2f
1:	l	%r15,\stack		# load target stack
2:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	.endm

	.macro	ADD64 high,low,timer
	al	\high,\timer
	al	\low,4+\timer
	brc	12,.+8
	ahi	\high,1
	.endm

	.macro	SUB64 high,low,timer
	sl	\high,\timer
	sl	\low,4+\timer
	brc	3,.+8
	ahi	\high,-1
	.endm

	.macro	UPDATE_VTIME high,low,enter_timer
	lm	\high,\low,__LC_EXIT_TIMER
	SUB64	\high,\low,\enter_timer
	ADD64	\high,\low,__LC_USER_TIMER
	stm	\high,\low,__LC_USER_TIMER
	lm	\high,\low,__LC_LAST_UPDATE_TIMER
	SUB64	\high,\low,__LC_EXIT_TIMER
	ADD64	\high,\low,__LC_SYSTEM_TIMER
	stm	\high,\low,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),\enter_timer
	.endm

	.macro REENABLE_IRQS
	st	%r8,__LC_RETURN_PSW
	ni	__LC_RETURN_PSW,0xbf
	ssm	__LC_RETURN_PSW
	.endm

	.section .kprobes.text, "ax"

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
ENTRY(__switch_to)
	stm	%r6,%r15,__SF_GPRS(%r15)	# store gprs of prev task
	st	%r15,__THREAD_ksp(%r2)		# store kernel stack of prev
	l	%r4,__THREAD_info(%r2)		# get thread_info of prev
	l	%r5,__THREAD_info(%r3)		# get thread_info of next
	lr	%r15,%r5
	ahi	%r15,STACK_INIT			# end of kernel stack of next
	st	%r3,__LC_CURRENT		# store task struct of next
	st	%r5,__LC_THREAD_INFO		# store thread info of next
	st	%r15,__LC_KERNEL_STACK		# store end of kernel stack
	lctl	%c4,%c4,__TASK_pid(%r3)		# load pid to control reg. 4
	mvc	__LC_CURRENT_PID(4,%r0),__TASK_pid(%r3)	# store pid of next
	l	%r15,__THREAD_ksp(%r3)		# load kernel stack of next
	tm	__TI_flags+3(%r4),_TIF_MCCK_PENDING # machine check pending?
	jz	0f
	ni	__TI_flags+3(%r4),255-_TIF_MCCK_PENDING	# clear flag in prev
	oi	__TI_flags+3(%r5),_TIF_MCCK_PENDING	# set it in next
0:	lm	%r6,%r15,__SF_GPRS(%r15)	# load gprs of next task
	br	%r14

__critical_start:
/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

ENTRY(system_call)
	stpt	__LC_SYNC_ENTER_TIMER
sysc_stm:
	stm	%r8,%r15,__LC_SAVE_AREA_SYNC
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
sysc_per:
	l	%r15,__LC_KERNEL_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)	# pointer to pt_regs
sysc_vtime:
	UPDATE_VTIME %r8,%r9,__LC_SYNC_ENTER_TIMER
	stm	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(32,%r11),__LC_SAVE_AREA_SYNC
	mvc	__PT_PSW(8,%r11),__LC_SVC_OLD_PSW
	mvc	__PT_INT_CODE(4,%r11),__LC_SVC_ILC
sysc_do_svc:
	oi	__TI_flags+3(%r12),_TIF_SYSCALL
	l	%r10,__TI_sysc_table(%r12)	# 31 bit system call table
	lh	%r8,__PT_INT_CODE+2(%r11)
	sla	%r8,2				# shift and test for svc0
	jnz	sysc_nr_ok
	# svc 0: system call number in %r1
	cl	%r1,BASED(.Lnr_syscalls)
	jnl	sysc_nr_ok
	sth	%r1,__PT_INT_CODE+2(%r11)
	lr	%r8,%r1
	sla	%r8,2
sysc_nr_ok:
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	st	%r2,__PT_ORIG_GPR2(%r11)
	st	%r7,STACK_FRAME_OVERHEAD(%r15)
	l	%r9,0(%r8,%r10)			# get system call addr.
	tm	__TI_flags+2(%r12),_TIF_TRACE >> 8
	jnz	sysc_tracesys
	basr	%r14,%r9			# call sys_xxxx
	st	%r2,__PT_R2(%r11)		# store return value

sysc_return:
	LOCKDEP_SYS_EXIT
sysc_tif:
	tm	__PT_PSW+1(%r11),0x01		# returning to user ?
	jno	sysc_restore
	tm	__TI_flags+3(%r12),_TIF_WORK_SVC
	jnz	sysc_work			# check for work
	ni	__TI_flags+3(%r12),255-_TIF_SYSCALL
sysc_restore:
	mvc	__LC_RETURN_PSW(8),__PT_PSW(%r11)
	stpt	__LC_EXIT_TIMER
	lm	%r0,%r15,__PT_R0(%r11)
	lpsw	__LC_RETURN_PSW
sysc_done:

#
# One of the work bits is on. Find out which one.
#
sysc_work:
	tm	__TI_flags+3(%r12),_TIF_MCCK_PENDING
	jo	sysc_mcck_pending
	tm	__TI_flags+3(%r12),_TIF_NEED_RESCHED
	jo	sysc_reschedule
	tm	__TI_flags+3(%r12),_TIF_PER_TRAP
	jo	sysc_singlestep
	tm	__TI_flags+3(%r12),_TIF_SIGPENDING
	jo	sysc_sigpending
	tm	__TI_flags+3(%r12),_TIF_NOTIFY_RESUME
	jo	sysc_notify_resume
	j	sysc_return		# beware of critical section cleanup

#
# _TIF_NEED_RESCHED is set, call schedule
#
sysc_reschedule:
	l	%r1,BASED(.Lschedule)
	la	%r14,BASED(sysc_return)
	br	%r1			# call schedule

#
# _TIF_MCCK_PENDING is set, call handler
#
sysc_mcck_pending:
	l	%r1,BASED(.Lhandle_mcck)
	la	%r14,BASED(sysc_return)
	br	%r1			# TIF bit will be cleared by handler

#
# _TIF_SIGPENDING is set, call do_signal
#
sysc_sigpending:
	lr	%r2,%r11		# pass pointer to pt_regs
	l	%r1,BASED(.Ldo_signal)
	basr	%r14,%r1		# call do_signal
	tm	__TI_flags+3(%r12),_TIF_SYSCALL
	jno	sysc_return
	lm	%r2,%r7,__PT_R2(%r11)	# load svc arguments
	xr	%r8,%r8			# svc 0 returns -ENOSYS
	clc	__PT_INT_CODE+2(2,%r11),BASED(.Lnr_syscalls+2)
	jnl	sysc_nr_ok		# invalid svc number -> do svc 0
	lh	%r8,__PT_INT_CODE+2(%r11)	# load new svc number
	sla	%r8,2
	j	sysc_nr_ok		# restart svc

#
# _TIF_NOTIFY_RESUME is set, call do_notify_resume
#
sysc_notify_resume:
	lr	%r2,%r11		# pass pointer to pt_regs
	l	%r1,BASED(.Ldo_notify_resume)
	la	%r14,BASED(sysc_return)
	br	%r1			# call do_notify_resume

#
# _TIF_PER_TRAP is set, call do_per_trap
#
sysc_singlestep:
	ni	__TI_flags+3(%r12),255-_TIF_PER_TRAP
	lr	%r2,%r11		# pass pointer to pt_regs
	l	%r1,BASED(.Ldo_per_trap)
	la	%r14,BASED(sysc_return)
	br	%r1			# call do_per_trap

#
# call tracehook_report_syscall_entry/tracehook_report_syscall_exit before
# and after the system call
#
sysc_tracesys:
	l	%r1,BASED(.Ltrace_enter)
	lr	%r2,%r11		# pass pointer to pt_regs
	la	%r3,0
	xr	%r0,%r0
	icm	%r0,3,__PT_INT_CODE+2(%r11)
	st	%r0,__PT_R2(%r11)
	basr	%r14,%r1		# call do_syscall_trace_enter
	cl	%r2,BASED(.Lnr_syscalls)
	jnl	sysc_tracenogo
	lr	%r8,%r2
	sll	%r8,2
	l	%r9,0(%r8,%r10)
sysc_tracego:
	lm	%r3,%r7,__PT_R3(%r11)
	st	%r7,STACK_FRAME_OVERHEAD(%r15)
	l	%r2,__PT_ORIG_GPR2(%r11)
	basr	%r14,%r9		# call sys_xxx
	st	%r2,__PT_R2(%r11)	# store return value
sysc_tracenogo:
	tm	__TI_flags+2(%r12),_TIF_TRACE >> 8
	jz	sysc_return
	l	%r1,BASED(.Ltrace_exit)
	lr	%r2,%r11		# pass pointer to pt_regs
	la	%r14,BASED(sysc_return)
	br	%r1			# call do_syscall_trace_exit

#
# a new process exits the kernel with ret_from_fork
#
ENTRY(ret_from_fork)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
	l	%r1,BASED(.Lschedule_tail)
	basr	%r14,%r1		# call schedule_tail
	TRACE_IRQS_ON
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	tm	__PT_PSW+1(%r11),0x01	# forking a kernel thread ?
	jne	sysc_tracenogo
	# it's a kernel thread
	lm	%r9,%r10,__PT_R9(%r11)	# load gprs
ENTRY(kernel_thread_starter)
	la	%r2,0(%r10)
	basr	%r14,%r9
	j	sysc_tracenogo

/*
 * Program check handler routine
 */

ENTRY(pgm_check_handler)
	stpt	__LC_SYNC_ENTER_TIMER
	stm	%r8,%r15,__LC_SAVE_AREA_SYNC
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
	lm	%r8,%r9,__LC_PGM_OLD_PSW
	tmh	%r8,0x0001		# test problem state bit
	jnz	1f			# -> fault in user space
	tmh	%r8,0x4000		# PER bit set in old PSW ?
	jnz	0f			# -> enabled, can't be a double fault
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jnz	pgm_svcper		# -> single stepped svc
0:	CHECK_STACK STACK_SIZE,__LC_SAVE_AREA_SYNC
	ahi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	2f
1:	UPDATE_VTIME %r14,%r15,__LC_SYNC_ENTER_TIMER
	l	%r15,__LC_KERNEL_STACK
2:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stm	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(32,%r11),__LC_SAVE_AREA_SYNC
	stm	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_INT_CODE(4,%r11),__LC_PGM_ILC
	mvc	__PT_INT_PARM_LONG(4,%r11),__LC_TRANS_EXC_CODE
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jz	0f
	l	%r1,__TI_task(%r12)
	tmh	%r8,0x0001		# kernel per event ?
	jz	pgm_kprobe
	oi	__TI_flags+3(%r12),_TIF_PER_TRAP
	mvc	__THREAD_per_address(4,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per_cause(2,%r1),__LC_PER_CAUSE
	mvc	__THREAD_per_paid(1,%r1),__LC_PER_PAID
0:	REENABLE_IRQS
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	l	%r1,BASED(.Ljump_table)
	la	%r10,0x7f
	n	%r10,__PT_INT_CODE(%r11)
	je	sysc_return
	sll	%r10,2
	l	%r1,0(%r10,%r1)		# load address of handler routine
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# branch to interrupt-handler
	j	sysc_return

#
# PER event in supervisor state, must be kprobes
#
pgm_kprobe:
	REENABLE_IRQS
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	l	%r1,BASED(.Ldo_per_trap)
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# call do_per_trap
	j	sysc_return

#
# single stepped system call
#
pgm_svcper:
	oi	__TI_flags+3(%r12),_TIF_PER_TRAP
	mvc	__LC_RETURN_PSW(4),__LC_SVC_NEW_PSW
	mvc	__LC_RETURN_PSW+4(4),BASED(.Lsysc_per)
	lpsw	__LC_RETURN_PSW		# branch to sysc_per and enable irqs

/*
 * IO interrupt handler routine
 */

ENTRY(io_int_handler)
	stck	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	stm	%r8,%r15,__LC_SAVE_AREA_ASYNC
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
	lm	%r8,%r9,__LC_IO_OLD_PSW
	tmh	%r8,0x0001		# interrupting from user ?
	jz	io_skip
	UPDATE_VTIME %r14,%r15,__LC_ASYNC_ENTER_TIMER
io_skip:
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_STACK,STACK_SHIFT
	stm	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(32,%r11),__LC_SAVE_AREA_ASYNC
	stm	%r8,%r9,__PT_PSW(%r11)
	TRACE_IRQS_OFF
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	l	%r1,BASED(.Ldo_IRQ)
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# call do_IRQ
io_return:
	LOCKDEP_SYS_EXIT
	TRACE_IRQS_ON
io_tif:
	tm	__TI_flags+3(%r12),_TIF_WORK_INT
	jnz	io_work			# there is work to do (signals etc.)
io_restore:
	mvc	__LC_RETURN_PSW(8),__PT_PSW(%r11)
	stpt	__LC_EXIT_TIMER
	lm	%r0,%r15,__PT_R0(%r11)
	lpsw	__LC_RETURN_PSW
io_done:

#
# There is work todo, find out in which context we have been interrupted:
# 1) if we return to user space we can do all _TIF_WORK_INT work
# 2) if we return to kernel code and preemptive scheduling is enabled check
#    the preemption counter and if it is zero call preempt_schedule_irq
# Before any work can be done, a switch to the kernel stack is required.
#
io_work:
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jo	io_work_user		# yes -> do resched & signal
#ifdef CONFIG_PREEMPT
	# check for preemptive scheduling
	icm	%r0,15,__TI_precount(%r12)
	jnz	io_restore		# preemption disabled
	tm	__TI_flags+3(%r12),_TIF_NEED_RESCHED
	jno	io_restore
	# switch to kernel stack
	l	%r1,__PT_R15(%r11)
	ahi	%r1,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(4,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lr	%r15,%r1
	# TRACE_IRQS_ON already done at io_return, call
	# TRACE_IRQS_OFF to keep things symmetrical
	TRACE_IRQS_OFF
	l	%r1,BASED(.Lpreempt_irq)
	basr	%r14,%r1		# call preempt_schedule_irq
	j	io_return
#else
	j	io_restore
#endif

#
# Need to do work before returning to userspace, switch to kernel stack
#
io_work_user:
	l	%r1,__LC_KERNEL_STACK
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(4,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lr	%r15,%r1

#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING, _TIF_NOTIFY_RESUME, _TIF_NEED_RESCHED
#		and _TIF_MCCK_PENDING
#
io_work_tif:
	tm	__TI_flags+3(%r12),_TIF_MCCK_PENDING
	jo	io_mcck_pending
	tm	__TI_flags+3(%r12),_TIF_NEED_RESCHED
	jo	io_reschedule
	tm	__TI_flags+3(%r12),_TIF_SIGPENDING
	jo	io_sigpending
	tm	__TI_flags+3(%r12),_TIF_NOTIFY_RESUME
	jo	io_notify_resume
	j	io_return		# beware of critical section cleanup

#
# _TIF_MCCK_PENDING is set, call handler
#
io_mcck_pending:
	# TRACE_IRQS_ON already done at io_return
	l	%r1,BASED(.Lhandle_mcck)
	basr	%r14,%r1		# TIF bit will be cleared by handler
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_NEED_RESCHED is set, call schedule
#
io_reschedule:
	# TRACE_IRQS_ON already done at io_return
	l	%r1,BASED(.Lschedule)
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	basr	%r14,%r1		# call scheduler
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_SIGPENDING is set, call do_signal
#
io_sigpending:
	# TRACE_IRQS_ON already done at io_return
	l	%r1,BASED(.Ldo_signal)
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# call do_signal
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_SIGPENDING is set, call do_signal
#
io_notify_resume:
	# TRACE_IRQS_ON already done at io_return
	l	%r1,BASED(.Ldo_notify_resume)
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# call do_notify_resume
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

/*
 * External interrupt handler routine
 */

ENTRY(ext_int_handler)
	stck	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	stm	%r8,%r15,__LC_SAVE_AREA_ASYNC
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
	lm	%r8,%r9,__LC_EXT_OLD_PSW
	tmh	%r8,0x0001		# interrupting from user ?
	jz	ext_skip
	UPDATE_VTIME %r14,%r15,__LC_ASYNC_ENTER_TIMER
ext_skip:
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_STACK,STACK_SHIFT
	stm	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(32,%r11),__LC_SAVE_AREA_ASYNC
	stm	%r8,%r9,__PT_PSW(%r11)
	TRACE_IRQS_OFF
	lr	%r2,%r11		# pass pointer to pt_regs
	l	%r3,__LC_EXT_CPU_ADDR	# get cpu address + interruption code
	l	%r4,__LC_EXT_PARAMS	# get external parameters
	l	%r1,BASED(.Ldo_extint)
	basr	%r14,%r1		# call do_extint
	j	io_return

/*
 * Load idle PSW. The second "half" of this function is in cleanup_idle.
 */
ENTRY(psw_idle)
	st	%r3,__SF_EMPTY(%r15)
	basr	%r1,0
	la	%r1,psw_idle_lpsw+4-.(%r1)
	st	%r1,__SF_EMPTY+4(%r15)
	oi	__SF_EMPTY+4(%r15),0x80
	stck	__CLOCK_IDLE_ENTER(%r2)
	stpt	__TIMER_IDLE_ENTER(%r2)
psw_idle_lpsw:
	lpsw	__SF_EMPTY(%r15)
	br	%r14
psw_idle_end:

__critical_end:

/*
 * Machine check handler routines
 */

ENTRY(mcck_int_handler)
	stck	__LC_MCCK_CLOCK
	spt	__LC_CPU_TIMER_SAVE_AREA	# revalidate cpu timer
	lm	%r0,%r15,__LC_GPREGS_SAVE_AREA	# revalidate gprs
	l	%r12,__LC_THREAD_INFO
	l	%r13,__LC_SVC_NEW_PSW+4
	lm	%r8,%r9,__LC_MCK_OLD_PSW
	tm	__LC_MCCK_CODE,0x80	# system damage?
	jo	mcck_panic		# yes -> rest of mcck code invalid
	la	%r14,__LC_CPU_TIMER_SAVE_AREA
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
	tm	__LC_MCCK_CODE+5,0x02	# stored cpu timer value valid?
	jo	3f
	la	%r14,__LC_SYNC_ENTER_TIMER
	clc	0(8,%r14),__LC_ASYNC_ENTER_TIMER
	jl	0f
	la	%r14,__LC_ASYNC_ENTER_TIMER
0:	clc	0(8,%r14),__LC_EXIT_TIMER
	jl	1f
	la	%r14,__LC_EXIT_TIMER
1:	clc	0(8,%r14),__LC_LAST_UPDATE_TIMER
	jl	2f
	la	%r14,__LC_LAST_UPDATE_TIMER
2:	spt	0(%r14)
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
3:	tm	__LC_MCCK_CODE+2,0x09	# mwp + ia of old psw valid?
	jno	mcck_panic		# no -> skip cleanup critical
	tm	%r8,0x0001		# interrupting from user ?
	jz	mcck_skip
	UPDATE_VTIME %r14,%r15,__LC_MCCK_ENTER_TIMER
mcck_skip:
	SWITCH_ASYNC __LC_GPREGS_SAVE_AREA+32,__LC_PANIC_STACK,PAGE_SHIFT
	stm	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(32,%r11),__LC_GPREGS_SAVE_AREA+32
	stm	%r8,%r9,__PT_PSW(%r11)
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	l	%r1,BASED(.Ldo_machine_check)
	lr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# call s390_do_machine_check
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jno	mcck_return
	l	%r1,__LC_KERNEL_STACK	# switch to kernel stack
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(4,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	lr	%r15,%r1
	ssm	__LC_PGM_NEW_PSW	# turn dat on, keep irqs off
	tm	__TI_flags+3(%r12),_TIF_MCCK_PENDING
	jno	mcck_return
	TRACE_IRQS_OFF
	l	%r1,BASED(.Lhandle_mcck)
	basr	%r14,%r1		# call s390_handle_mcck
	TRACE_IRQS_ON
mcck_return:
	mvc	__LC_RETURN_MCCK_PSW(8),__PT_PSW(%r11) # move return PSW
	tm	__LC_RETURN_MCCK_PSW+1,0x01 # returning to user ?
	jno	0f
	lm	%r0,%r15,__PT_R0(%r11)
	stpt	__LC_EXIT_TIMER
	lpsw	__LC_RETURN_MCCK_PSW
0:	lm	%r0,%r15,__PT_R0(%r11)
	lpsw	__LC_RETURN_MCCK_PSW

mcck_panic:
	l	%r14,__LC_PANIC_STACK
	slr	%r14,%r15
	sra	%r14,PAGE_SHIFT
	jz	0f
	l	%r15,__LC_PANIC_STACK
	j	mcck_skip
0:	ahi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	mcck_skip

#
# PSW restart interrupt handler
#
ENTRY(restart_int_handler)
	st	%r15,__LC_SAVE_AREA_RESTART
	l	%r15,__LC_RESTART_STACK
	ahi	%r15,-__PT_SIZE			# create pt_regs on stack
	xc	0(__PT_SIZE,%r15),0(%r15)
	stm	%r0,%r14,__PT_R0(%r15)
	mvc	__PT_R15(4,%r15),__LC_SAVE_AREA_RESTART
	mvc	__PT_PSW(8,%r15),__LC_RST_OLD_PSW # store restart old psw
	ahi	%r15,-STACK_FRAME_OVERHEAD	# create stack frame on stack
	xc	0(STACK_FRAME_OVERHEAD,%r15),0(%r15)
	l	%r1,__LC_RESTART_FN		# load fn, parm & source cpu
	l	%r2,__LC_RESTART_DATA
	l	%r3,__LC_RESTART_SOURCE
	ltr	%r3,%r3				# test source cpu address
	jm	1f				# negative -> skip source stop
0:	sigp	%r4,%r3,SIGP_SENSE		# sigp sense to source cpu
	brc	10,0b				# wait for status stored
1:	basr	%r14,%r1			# call function
	stap	__SF_EMPTY(%r15)		# store cpu address
	lh	%r3,__SF_EMPTY(%r15)
2:	sigp	%r4,%r3,SIGP_STOP		# sigp stop to current cpu
	brc	2,2b
3:	j	3b

	.section .kprobes.text, "ax"

#ifdef CONFIG_CHECK_STACK
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
stack_overflow:
	l	%r15,__LC_PANIC_STACK	# change to panic stack
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stm	%r0,%r7,__PT_R0(%r11)
	stm	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_R8(32,%r11),0(%r14)
	l	%r1,BASED(1f)
	xc	__SF_BACKCHAIN(4,%r15),__SF_BACKCHAIN(%r15)
	lr	%r2,%r11		# pass pointer to pt_regs
	br	%r1			# branch to kernel_stack_overflow
1:	.long	kernel_stack_overflow
#endif

cleanup_table:
	.long	system_call + 0x80000000
	.long	sysc_do_svc + 0x80000000
	.long	sysc_tif + 0x80000000
	.long	sysc_restore + 0x80000000
	.long	sysc_done + 0x80000000
	.long	io_tif + 0x80000000
	.long	io_restore + 0x80000000
	.long	io_done + 0x80000000
	.long	psw_idle + 0x80000000
	.long	psw_idle_end + 0x80000000

cleanup_critical:
	cl	%r9,BASED(cleanup_table)	# system_call
	jl	0f
	cl	%r9,BASED(cleanup_table+4)	# sysc_do_svc
	jl	cleanup_system_call
	cl	%r9,BASED(cleanup_table+8)	# sysc_tif
	jl	0f
	cl	%r9,BASED(cleanup_table+12)	# sysc_restore
	jl	cleanup_sysc_tif
	cl	%r9,BASED(cleanup_table+16)	# sysc_done
	jl	cleanup_sysc_restore
	cl	%r9,BASED(cleanup_table+20)	# io_tif
	jl	0f
	cl	%r9,BASED(cleanup_table+24)	# io_restore
	jl	cleanup_io_tif
	cl	%r9,BASED(cleanup_table+28)	# io_done
	jl	cleanup_io_restore
	cl	%r9,BASED(cleanup_table+32)	# psw_idle
	jl	0f
	cl	%r9,BASED(cleanup_table+36)	# psw_idle_end
	jl	cleanup_idle
0:	br	%r14

cleanup_system_call:
	# check if stpt has been executed
	cl	%r9,BASED(cleanup_system_call_insn)
	jh	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_ASYNC_ENTER_TIMER
	chi	%r11,__LC_SAVE_AREA_ASYNC
	je	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_MCCK_ENTER_TIMER
0:	# check if stm has been executed
	cl	%r9,BASED(cleanup_system_call_insn+4)
	jh	0f
	mvc	__LC_SAVE_AREA_SYNC(32),0(%r11)
0:	# set up saved registers r12, and r13
	st	%r12,16(%r11)		# r12 thread-info pointer
	st	%r13,20(%r11)		# r13 literal-pool pointer
	# check if the user time calculation has been done
	cl	%r9,BASED(cleanup_system_call_insn+8)
	jh	0f
	l	%r10,__LC_EXIT_TIMER
	l	%r15,__LC_EXIT_TIMER+4
	SUB64	%r10,%r15,__LC_SYNC_ENTER_TIMER
	ADD64	%r10,%r15,__LC_USER_TIMER
	st	%r10,__LC_USER_TIMER
	st	%r15,__LC_USER_TIMER+4
0:	# check if the system time calculation has been done
	cl	%r9,BASED(cleanup_system_call_insn+12)
	jh	0f
	l	%r10,__LC_LAST_UPDATE_TIMER
	l	%r15,__LC_LAST_UPDATE_TIMER+4
	SUB64	%r10,%r15,__LC_EXIT_TIMER
	ADD64	%r10,%r15,__LC_SYSTEM_TIMER
	st	%r10,__LC_SYSTEM_TIMER
	st	%r15,__LC_SYSTEM_TIMER+4
0:	# update accounting time stamp
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	# set up saved register 11
	l	%r15,__LC_KERNEL_STACK
	la	%r9,STACK_FRAME_OVERHEAD(%r15)
	st	%r9,12(%r11)		# r11 pt_regs pointer
	# fill pt_regs
	mvc	__PT_R8(32,%r9),__LC_SAVE_AREA_SYNC
	stm	%r0,%r7,__PT_R0(%r9)
	mvc	__PT_PSW(8,%r9),__LC_SVC_OLD_PSW
	mvc	__PT_INT_CODE(4,%r9),__LC_SVC_ILC
	# setup saved register 15
	st	%r15,28(%r11)		# r15 stack pointer
	# set new psw address and exit
	l	%r9,BASED(cleanup_table+4)	# sysc_do_svc + 0x80000000
	br	%r14
cleanup_system_call_insn:
	.long	system_call + 0x80000000
	.long	sysc_stm + 0x80000000
	.long	sysc_vtime + 0x80000000 + 36
	.long	sysc_vtime + 0x80000000 + 76

cleanup_sysc_tif:
	l	%r9,BASED(cleanup_table+8)	# sysc_tif + 0x80000000
	br	%r14

cleanup_sysc_restore:
	cl	%r9,BASED(cleanup_sysc_restore_insn)
	jhe	0f
	l	%r9,12(%r11)		# get saved pointer to pt_regs
	mvc	__LC_RETURN_PSW(8),__PT_PSW(%r9)
	mvc	0(32,%r11),__PT_R8(%r9)
	lm	%r0,%r7,__PT_R0(%r9)
0:	lm	%r8,%r9,__LC_RETURN_PSW
	br	%r14
cleanup_sysc_restore_insn:
	.long	sysc_done - 4 + 0x80000000

cleanup_io_tif:
	l	%r9,BASED(cleanup_table+20)	# io_tif + 0x80000000
	br	%r14

cleanup_io_restore:
	cl	%r9,BASED(cleanup_io_restore_insn)
	jhe	0f
	l	%r9,12(%r11)		# get saved r11 pointer to pt_regs
	mvc	__LC_RETURN_PSW(8),__PT_PSW(%r9)
	mvc	0(32,%r11),__PT_R8(%r9)
	lm	%r0,%r7,__PT_R0(%r9)
0:	lm	%r8,%r9,__LC_RETURN_PSW
	br	%r14
cleanup_io_restore_insn:
	.long	io_done - 4 + 0x80000000

cleanup_idle:
	# copy interrupt clock & cpu timer
	mvc	__CLOCK_IDLE_EXIT(8,%r2),__LC_INT_CLOCK
	mvc	__TIMER_IDLE_EXIT(8,%r2),__LC_ASYNC_ENTER_TIMER
	chi	%r11,__LC_SAVE_AREA_ASYNC
	je	0f
	mvc	__CLOCK_IDLE_EXIT(8,%r2),__LC_MCCK_CLOCK
	mvc	__TIMER_IDLE_EXIT(8,%r2),__LC_MCCK_ENTER_TIMER
0:	# check if stck has been executed
	cl	%r9,BASED(cleanup_idle_insn)
	jhe	1f
	mvc	__CLOCK_IDLE_ENTER(8,%r2),__CLOCK_IDLE_EXIT(%r2)
	mvc	__TIMER_IDLE_ENTER(8,%r2),__TIMER_IDLE_EXIT(%r3)
1:	# account system time going idle
	lm	%r9,%r10,__LC_STEAL_TIMER
	ADD64	%r9,%r10,__CLOCK_IDLE_ENTER(%r2)
	SUB64	%r9,%r10,__LC_LAST_UPDATE_CLOCK
	stm	%r9,%r10,__LC_STEAL_TIMER
	mvc	__LC_LAST_UPDATE_CLOCK(8),__CLOCK_IDLE_EXIT(%r2)
	lm	%r9,%r10,__LC_SYSTEM_TIMER
	ADD64	%r9,%r10,__LC_LAST_UPDATE_TIMER
	SUB64	%r9,%r10,__TIMER_IDLE_ENTER(%r2)
	stm	%r9,%r10,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__TIMER_IDLE_EXIT(%r2)
	# prepare return psw
	n	%r8,BASED(cleanup_idle_wait)	# clear wait state bit
	l	%r9,24(%r11)			# return from psw_idle
	br	%r14
cleanup_idle_insn:
	.long	psw_idle_lpsw + 0x80000000
cleanup_idle_wait:
	.long	0xfffdffff

/*
 * Integer constants
 */
	.align	4
.Lnr_syscalls:
	.long	NR_syscalls
.Lvtimer_max:
	.quad	0x7fffffffffffffff

/*
 * Symbol constants
 */
.Ldo_machine_check:	.long	s390_do_machine_check
.Lhandle_mcck:		.long	s390_handle_mcck
.Ldo_IRQ:		.long	do_IRQ
.Ldo_extint:		.long	do_extint
.Ldo_signal:		.long	do_signal
.Ldo_notify_resume:	.long	do_notify_resume
.Ldo_per_trap:		.long	do_per_trap
.Ljump_table:		.long	pgm_check_table
.Lschedule:		.long	schedule
#ifdef CONFIG_PREEMPT
.Lpreempt_irq:		.long	preempt_schedule_irq
#endif
.Ltrace_enter:		.long	do_syscall_trace_enter
.Ltrace_exit:		.long	do_syscall_trace_exit
.Lschedule_tail:	.long	schedule_tail
.Lsysc_per:		.long	sysc_per + 0x80000000
#ifdef CONFIG_TRACE_IRQFLAGS
.Lhardirqs_on:		.long	trace_hardirqs_on_caller
.Lhardirqs_off:		.long	trace_hardirqs_off_caller
#endif
#ifdef CONFIG_LOCKDEP
.Llockdep_sys_exit:	.long	lockdep_sys_exit
#endif
.Lcritical_start:	.long	__critical_start + 0x80000000
.Lcritical_length:	.long	__critical_end - __critical_start

		.section .rodata, "a"
#define SYSCALL(esa,esame,emu)	.long esa
	.globl	sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL
