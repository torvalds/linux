/*
 *  arch/s390/kernel/entry64.S
 *    S390 low-level entry points.
 *
 *    Copyright (C) IBM Corp. 1999,2010
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Hartmut Penner (hp@de.ibm.com),
 *		 Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/page.h>

/*
 * Stack layout for the system_call stack entry.
 * The first few entries are identical to the user_regs_struct.
 */
SP_PTREGS    =	STACK_FRAME_OVERHEAD
SP_ARGS      =	STACK_FRAME_OVERHEAD + __PT_ARGS
SP_PSW	     =	STACK_FRAME_OVERHEAD + __PT_PSW
SP_R0	     =	STACK_FRAME_OVERHEAD + __PT_GPRS
SP_R1	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 8
SP_R2	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 16
SP_R3	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 24
SP_R4	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 32
SP_R5	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 40
SP_R6	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 48
SP_R7	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 56
SP_R8	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 64
SP_R9	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 72
SP_R10	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 80
SP_R11	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 88
SP_R12	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 96
SP_R13	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 104
SP_R14	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 112
SP_R15	     =	STACK_FRAME_OVERHEAD + __PT_GPRS + 120
SP_ORIG_R2   =	STACK_FRAME_OVERHEAD + __PT_ORIG_GPR2
SP_ILC	     =	STACK_FRAME_OVERHEAD + __PT_ILC
SP_SVCNR      =	STACK_FRAME_OVERHEAD + __PT_SVCNR
SP_SIZE      =	STACK_FRAME_OVERHEAD + __PT_SIZE

STACK_SHIFT = PAGE_SHIFT + THREAD_ORDER
STACK_SIZE  = 1 << STACK_SHIFT

_TIF_WORK_SVC = (_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_NEED_RESCHED | \
		 _TIF_MCCK_PENDING | _TIF_RESTART_SVC | _TIF_SINGLE_STEP )
_TIF_WORK_INT = (_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_NEED_RESCHED | \
		 _TIF_MCCK_PENDING)
_TIF_SYSCALL = (_TIF_SYSCALL_TRACE>>8 | _TIF_SYSCALL_AUDIT>>8 | \
		_TIF_SECCOMP>>8 | _TIF_SYSCALL_TRACEPOINT>>8)

#define BASED(name) name-system_call(%r13)

	.macro	HANDLE_SIE_INTERCEPT
#if defined(CONFIG_KVM) || defined(CONFIG_KVM_MODULE)
	lg	%r3,__LC_SIE_HOOK
	ltgr	%r3,%r3
	jz	0f
	basr	%r14,%r3
0:
#endif
	.endm

#ifdef CONFIG_TRACE_IRQFLAGS
	.macro	TRACE_IRQS_ON
	basr	%r2,%r0
	brasl	%r14,trace_hardirqs_on_caller
	.endm

	.macro	TRACE_IRQS_OFF
	basr	%r2,%r0
	brasl	%r14,trace_hardirqs_off_caller
	.endm

	.macro TRACE_IRQS_CHECK_ON
	tm	SP_PSW(%r15),0x03	# irqs enabled?
	jz	0f
	TRACE_IRQS_ON
0:
	.endm

	.macro TRACE_IRQS_CHECK_OFF
	tm	SP_PSW(%r15),0x03	# irqs enabled?
	jz	0f
	TRACE_IRQS_OFF
0:
	.endm
#else
#define TRACE_IRQS_ON
#define TRACE_IRQS_OFF
#define TRACE_IRQS_CHECK_ON
#define TRACE_IRQS_CHECK_OFF
#endif

#ifdef CONFIG_LOCKDEP
	.macro	LOCKDEP_SYS_EXIT
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	jz	0f
	brasl	%r14,lockdep_sys_exit
0:
	.endm
#else
#define LOCKDEP_SYS_EXIT
#endif

	.macro	UPDATE_VTIME lc_from,lc_to,lc_sum
	lg	%r10,\lc_from
	slg	%r10,\lc_to
	alg	%r10,\lc_sum
	stg	%r10,\lc_sum
	.endm

/*
 * Register usage in interrupt handlers:
 *    R9  - pointer to current task structure
 *    R13 - pointer to literal pool
 *    R14 - return register for function calls
 *    R15 - kernel stack pointer
 */

	.macro	SAVE_ALL_SVC psworg,savearea
	stmg	%r11,%r15,\savearea
	lg	%r15,__LC_KERNEL_STACK	# problem state -> load ksp
	aghi	%r15,-SP_SIZE		# make room for registers & psw
	lg	%r11,__LC_LAST_BREAK
	.endm

	.macro	SAVE_ALL_PGM psworg,savearea
	stmg	%r11,%r15,\savearea
	tm	\psworg+1,0x01		# test problem state bit
#ifdef CONFIG_CHECK_STACK
	jnz	1f
	tml	%r15,STACK_SIZE - CONFIG_STACK_GUARD
	jnz	2f
	la	%r12,\psworg
	j	stack_overflow
#else
	jz	2f
#endif
1:	lg	%r15,__LC_KERNEL_STACK	# problem state -> load ksp
2:	aghi	%r15,-SP_SIZE		# make room for registers & psw
	larl	%r13,system_call
	lg	%r11,__LC_LAST_BREAK
	.endm

	.macro	SAVE_ALL_ASYNC psworg,savearea
	stmg	%r11,%r15,\savearea
	larl	%r13,system_call
	lg	%r11,__LC_LAST_BREAK
	la	%r12,\psworg
	tm	\psworg+1,0x01		# test problem state bit
	jnz	1f			# from user -> load kernel stack
	clc	\psworg+8(8),BASED(.Lcritical_end)
	jhe	0f
	clc	\psworg+8(8),BASED(.Lcritical_start)
	jl	0f
	brasl	%r14,cleanup_critical
	tm	1(%r12),0x01		# retest problem state after cleanup
	jnz	1f
0:	lg	%r14,__LC_ASYNC_STACK	# are we already on the async. stack ?
	slgr	%r14,%r15
	srag	%r14,%r14,STACK_SHIFT
#ifdef CONFIG_CHECK_STACK
	jnz	1f
	tml	%r15,STACK_SIZE - CONFIG_STACK_GUARD
	jnz	2f
	j	stack_overflow
#else
	jz	2f
#endif
1:	lg	%r15,__LC_ASYNC_STACK	# load async stack
2:	aghi	%r15,-SP_SIZE		# make room for registers & psw
	.endm

	.macro	CREATE_STACK_FRAME savearea
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stg	%r2,SP_ORIG_R2(%r15)	# store original content of gpr 2
	mvc	SP_R11(40,%r15),\savearea # move %r11-%r15 to stack
	stmg	%r0,%r10,SP_R0(%r15)	# store gprs %r0-%r10 to kernel stack
	.endm

	.macro	RESTORE_ALL psworg,sync
	mvc	\psworg(16),SP_PSW(%r15) # move user PSW to lowcore
	.if !\sync
	ni	\psworg+1,0xfd		# clear wait state bit
	.endif
	lg	%r14,__LC_VDSO_PER_CPU
	lmg	%r0,%r13,SP_R0(%r15)	# load gprs 0-13 of user
	stpt	__LC_EXIT_TIMER
	mvc	__VDSO_ECTG_BASE(16,%r14),__LC_EXIT_TIMER
	lmg	%r14,%r15,SP_R14(%r15)	# load grps 14-15 of user
	lpswe	\psworg			# back to caller
	.endm

	.macro	LAST_BREAK
	srag	%r10,%r11,23
	jz	0f
	stg	%r11,__TI_last_break(%r12)
0:
	.endm

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
	.globl	__switch_to
__switch_to:
	tm	__THREAD_per+4(%r3),0xe8 # is the new process using per ?
	jz	__switch_to_noper		# if not we're fine
	stctg	%c9,%c11,__SF_EMPTY(%r15)# We are using per stuff
	clc	__THREAD_per(24,%r3),__SF_EMPTY(%r15)
	je	__switch_to_noper	     # we got away without bashing TLB's
	lctlg	%c9,%c11,__THREAD_per(%r3)	# Nope we didn't
__switch_to_noper:
	lg	%r4,__THREAD_info(%r2)		    # get thread_info of prev
	tm	__TI_flags+7(%r4),_TIF_MCCK_PENDING # machine check pending?
	jz	__switch_to_no_mcck
	ni	__TI_flags+7(%r4),255-_TIF_MCCK_PENDING # clear flag in prev
	lg	%r4,__THREAD_info(%r3)		    # get thread_info of next
	oi	__TI_flags+7(%r4),_TIF_MCCK_PENDING # set it in next
__switch_to_no_mcck:
	stmg	%r6,%r15,__SF_GPRS(%r15)# store __switch_to registers of prev task
	stg	%r15,__THREAD_ksp(%r2)	# store kernel stack to prev->tss.ksp
	lg	%r15,__THREAD_ksp(%r3)	# load kernel stack from next->tss.ksp
	lmg	%r6,%r15,__SF_GPRS(%r15)# load __switch_to registers of next task
	stg	%r3,__LC_CURRENT	# __LC_CURRENT = current task struct
	lctl	%c4,%c4,__TASK_pid(%r3) # load pid to control reg. 4
	lg	%r3,__THREAD_info(%r3)	# load thread_info from task struct
	stg	%r3,__LC_THREAD_INFO
	aghi	%r3,STACK_SIZE
	stg	%r3,__LC_KERNEL_STACK	# __LC_KERNEL_STACK = new kernel stack
	br	%r14

__critical_start:
/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

	.globl	system_call
system_call:
	stpt	__LC_SYNC_ENTER_TIMER
sysc_saveall:
	SAVE_ALL_SVC __LC_SVC_OLD_PSW,__LC_SAVE_AREA
	CREATE_STACK_FRAME __LC_SAVE_AREA
	mvc	SP_PSW(16,%r15),__LC_SVC_OLD_PSW
	mvc	SP_ILC(4,%r15),__LC_SVC_ILC
	stg	%r7,SP_ARGS(%r15)
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
sysc_vtime:
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
sysc_stime:
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
sysc_update:
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	LAST_BREAK
sysc_do_svc:
	llgh	%r7,SP_SVCNR(%r15)
	slag	%r7,%r7,2	# shift and test for svc 0
	jnz	sysc_nr_ok
	# svc 0: system call number in %r1
	llgfr	%r1,%r1		# clear high word in r1
	cghi	%r1,NR_syscalls
	jnl	sysc_nr_ok
	sth	%r1,SP_SVCNR(%r15)
	slag	%r7,%r1,2	# shift and test for svc 0
sysc_nr_ok:
	larl	%r10,sys_call_table
#ifdef CONFIG_COMPAT
	tm	__TI_flags+5(%r12),(_TIF_31BIT>>16)  # running in 31 bit mode ?
	jno	sysc_noemu
	larl	%r10,sys_call_table_emu  # use 31 bit emulation system calls
sysc_noemu:
#endif
	tm	__TI_flags+6(%r12),_TIF_SYSCALL
	lgf	%r8,0(%r7,%r10) # load address of system call routine
	jnz	sysc_tracesys
	basr	%r14,%r8	# call sys_xxxx
	stg	%r2,SP_R2(%r15) # store return value (change R2 on stack)

sysc_return:
	LOCKDEP_SYS_EXIT
sysc_tif:
	tm	__TI_flags+7(%r12),_TIF_WORK_SVC
	jnz	sysc_work	# there is work to do (signals etc.)
sysc_restore:
	RESTORE_ALL __LC_RETURN_PSW,1
sysc_done:

#
# There is work to do, but first we need to check if we return to userspace.
#
sysc_work:
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	jno	sysc_restore

#
# One of the work bits is on. Find out which one.
#
sysc_work_tif:
	tm	__TI_flags+7(%r12),_TIF_MCCK_PENDING
	jo	sysc_mcck_pending
	tm	__TI_flags+7(%r12),_TIF_NEED_RESCHED
	jo	sysc_reschedule
	tm	__TI_flags+7(%r12),_TIF_SIGPENDING
	jo	sysc_sigpending
	tm	__TI_flags+7(%r12),_TIF_NOTIFY_RESUME
	jo	sysc_notify_resume
	tm	__TI_flags+7(%r12),_TIF_RESTART_SVC
	jo	sysc_restart
	tm	__TI_flags+7(%r12),_TIF_SINGLE_STEP
	jo	sysc_singlestep
	j	sysc_return		# beware of critical section cleanup

#
# _TIF_NEED_RESCHED is set, call schedule
#
sysc_reschedule:
	larl	%r14,sysc_return
	jg	schedule		# return point is sysc_return

#
# _TIF_MCCK_PENDING is set, call handler
#
sysc_mcck_pending:
	larl	%r14,sysc_return
	jg	s390_handle_mcck	# TIF bit will be cleared by handler

#
# _TIF_SIGPENDING is set, call do_signal
#
sysc_sigpending:
	ni	__TI_flags+7(%r12),255-_TIF_SINGLE_STEP # clear TIF_SINGLE_STEP
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	brasl	%r14,do_signal		# call do_signal
	tm	__TI_flags+7(%r12),_TIF_RESTART_SVC
	jo	sysc_restart
	tm	__TI_flags+7(%r12),_TIF_SINGLE_STEP
	jo	sysc_singlestep
	j	sysc_return

#
# _TIF_NOTIFY_RESUME is set, call do_notify_resume
#
sysc_notify_resume:
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	larl	%r14,sysc_return
	jg	do_notify_resume	# call do_notify_resume

#
# _TIF_RESTART_SVC is set, set up registers and restart svc
#
sysc_restart:
	ni	__TI_flags+7(%r12),255-_TIF_RESTART_SVC # clear TIF_RESTART_SVC
	lg	%r7,SP_R2(%r15)		# load new svc number
	mvc	SP_R2(8,%r15),SP_ORIG_R2(%r15) # restore first argument
	lmg	%r2,%r6,SP_R2(%r15)	# load svc arguments
	sth	%r7,SP_SVCNR(%r15)
	slag	%r7,%r7,2
	j	sysc_nr_ok		# restart svc

#
# _TIF_SINGLE_STEP is set, call do_single_step
#
sysc_singlestep:
	ni	__TI_flags+7(%r12),255-_TIF_SINGLE_STEP	# clear TIF_SINGLE_STEP
	xc	SP_SVCNR(2,%r15),SP_SVCNR(%r15)		# clear svc number
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	larl	%r14,sysc_return	# load adr. of system return
	jg	do_single_step		# branch to do_sigtrap

#
# call tracehook_report_syscall_entry/tracehook_report_syscall_exit before
# and after the system call
#
sysc_tracesys:
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	la	%r3,0
	llgh	%r0,SP_SVCNR(%r15)
	stg	%r0,SP_R2(%r15)
	brasl	%r14,do_syscall_trace_enter
	lghi	%r0,NR_syscalls
	clgr	%r0,%r2
	jnh	sysc_tracenogo
	sllg	%r7,%r2,2		# svc number *4
	lgf	%r8,0(%r7,%r10)
sysc_tracego:
	lmg	%r3,%r6,SP_R3(%r15)
	lg	%r2,SP_ORIG_R2(%r15)
	basr	%r14,%r8		# call sys_xxx
	stg	%r2,SP_R2(%r15)		# store return value
sysc_tracenogo:
	tm	__TI_flags+6(%r12),_TIF_SYSCALL
	jz	sysc_return
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	larl	%r14,sysc_return	# return point is sysc_return
	jg	do_syscall_trace_exit

#
# a new process exits the kernel with ret_from_fork
#
	.globl	ret_from_fork
ret_from_fork:
	lg	%r13,__LC_SVC_NEW_PSW+8
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# forking a kernel thread ?
	jo	0f
	stg	%r15,SP_R15(%r15)	# store stack pointer for new kthread
0:	brasl	%r14,schedule_tail
	TRACE_IRQS_ON
	stosm	24(%r15),0x03		# reenable interrupts
	j	sysc_tracenogo

#
# kernel_execve function needs to deal with pt_regs that is not
# at the usual place
#
	.globl	kernel_execve
kernel_execve:
	stmg	%r12,%r15,96(%r15)
	lgr	%r14,%r15
	aghi	%r15,-SP_SIZE
	stg	%r14,__SF_BACKCHAIN(%r15)
	la	%r12,SP_PTREGS(%r15)
	xc	0(__PT_SIZE,%r12),0(%r12)
	lgr	%r5,%r12
	brasl	%r14,do_execve
	ltgfr	%r2,%r2
	je	0f
	aghi	%r15,SP_SIZE
	lmg	%r12,%r15,96(%r15)
	br	%r14
	# execve succeeded.
0:	stnsm	__SF_EMPTY(%r15),0xfc	# disable interrupts
#	TRACE_IRQS_OFF
	lg	%r15,__LC_KERNEL_STACK	# load ksp
	aghi	%r15,-SP_SIZE		# make room for registers & psw
	lg	%r13,__LC_SVC_NEW_PSW+8
	mvc	SP_PTREGS(__PT_SIZE,%r15),0(%r12)	# copy pt_regs
	lg	%r12,__LC_THREAD_INFO
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
#	TRACE_IRQS_ON
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	brasl	%r14,execve_tail
	j	sysc_return

/*
 * Program check handler routine
 */

	.globl	pgm_check_handler
pgm_check_handler:
/*
 * First we need to check for a special case:
 * Single stepping an instruction that disables the PER event mask will
 * cause a PER event AFTER the mask has been set. Example: SVC or LPSW.
 * For a single stepped SVC the program check handler gets control after
 * the SVC new PSW has been loaded. But we want to execute the SVC first and
 * then handle the PER event. Therefore we update the SVC old PSW to point
 * to the pgm_check_handler and branch to the SVC handler after we checked
 * if we have to load the kernel stack register.
 * For every other possible cause for PER event without the PER mask set
 * we just ignore the PER event (FIXME: is there anything we have to do
 * for LPSW?).
 */
	stpt	__LC_SYNC_ENTER_TIMER
	tm	__LC_PGM_INT_CODE+1,0x80 # check whether we got a per exception
	jnz	pgm_per 		 # got per exception -> special case
	SAVE_ALL_PGM __LC_PGM_OLD_PSW,__LC_SAVE_AREA
	CREATE_STACK_FRAME __LC_SAVE_AREA
	xc	SP_ILC(4,%r15),SP_ILC(%r15)
	mvc	SP_PSW(16,%r15),__LC_PGM_OLD_PSW
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	jz	pgm_no_vtime
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	LAST_BREAK
pgm_no_vtime:
	HANDLE_SIE_INTERCEPT
	TRACE_IRQS_CHECK_OFF
	stg	%r11,SP_ARGS(%r15)
	lgf	%r3,__LC_PGM_ILC	# load program interruption code
	lghi	%r8,0x7f
	ngr	%r8,%r3
pgm_do_call:
	sll	%r8,3
	larl	%r1,pgm_check_table
	lg	%r1,0(%r8,%r1)		# load address of handler routine
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	basr	%r14,%r1		# branch to interrupt-handler
pgm_exit:
	TRACE_IRQS_CHECK_ON
	j	sysc_return

#
# handle per exception
#
pgm_per:
	tm	__LC_PGM_OLD_PSW,0x40	# test if per event recording is on
	jnz	pgm_per_std		# ok, normal per event from user space
# ok its one of the special cases, now we need to find out which one
	clc	__LC_PGM_OLD_PSW(16),__LC_SVC_NEW_PSW
	je	pgm_svcper
# no interesting special case, ignore PER event
	lpswe	__LC_PGM_OLD_PSW

#
# Normal per exception
#
pgm_per_std:
	SAVE_ALL_PGM __LC_PGM_OLD_PSW,__LC_SAVE_AREA
	CREATE_STACK_FRAME __LC_SAVE_AREA
	mvc	SP_PSW(16,%r15),__LC_PGM_OLD_PSW
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	jz	pgm_no_vtime2
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	LAST_BREAK
pgm_no_vtime2:
	HANDLE_SIE_INTERCEPT
	TRACE_IRQS_CHECK_OFF
	lg	%r1,__TI_task(%r12)
	tm	SP_PSW+1(%r15),0x01	# kernel per event ?
	jz	kernel_per
	mvc	__THREAD_per+__PER_atmid(2,%r1),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(8,%r1),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r1),__LC_PER_ACCESS_ID
	oi	__TI_flags+7(%r12),_TIF_SINGLE_STEP # set TIF_SINGLE_STEP
	lgf	%r3,__LC_PGM_ILC	# load program interruption code
	lghi	%r8,0x7f
	ngr	%r8,%r3			# clear per-event-bit and ilc
	je	pgm_exit2
	sll	%r8,3
	larl	%r1,pgm_check_table
	lg	%r1,0(%r8,%r1)		# load address of handler routine
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	basr	%r14,%r1		# branch to interrupt-handler
pgm_exit2:
	TRACE_IRQS_ON
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	j	sysc_return

#
# it was a single stepped SVC that is causing all the trouble
#
pgm_svcper:
	SAVE_ALL_PGM __LC_SVC_OLD_PSW,__LC_SAVE_AREA
	CREATE_STACK_FRAME __LC_SAVE_AREA
	mvc	SP_PSW(16,%r15),__LC_SVC_OLD_PSW
	mvc	SP_ILC(4,%r15),__LC_SVC_ILC
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	LAST_BREAK
	TRACE_IRQS_OFF
	lg	%r8,__TI_task(%r12)
	mvc	__THREAD_per+__PER_atmid(2,%r8),__LC_PER_ATMID
	mvc	__THREAD_per+__PER_address(8,%r8),__LC_PER_ADDRESS
	mvc	__THREAD_per+__PER_access_id(1,%r8),__LC_PER_ACCESS_ID
	oi	__TI_flags+7(%r12),_TIF_SINGLE_STEP # set TIF_SINGLE_STEP
	TRACE_IRQS_ON
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	lmg	%r2,%r6,SP_R2(%r15)	# load svc arguments
	j	sysc_do_svc

#
# per was called from kernel, must be kprobes
#
kernel_per:
	xc	SP_SVCNR(2,%r15),SP_SVCNR(%r15)	# clear svc number
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	brasl	%r14,do_single_step
	j	pgm_exit

/*
 * IO interrupt handler routine
 */
	.globl io_int_handler
io_int_handler:
	stck	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	SAVE_ALL_ASYNC __LC_IO_OLD_PSW,__LC_SAVE_AREA+40
	CREATE_STACK_FRAME __LC_SAVE_AREA+40
	mvc	SP_PSW(16,%r15),0(%r12)	# move user PSW to stack
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	jz	io_no_vtime
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_ASYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_ASYNC_ENTER_TIMER
	LAST_BREAK
io_no_vtime:
	HANDLE_SIE_INTERCEPT
	TRACE_IRQS_OFF
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	brasl	%r14,do_IRQ		# call standard irq handler
io_return:
	LOCKDEP_SYS_EXIT
	TRACE_IRQS_ON
io_tif:
	tm	__TI_flags+7(%r12),_TIF_WORK_INT
	jnz	io_work 		# there is work to do (signals etc.)
io_restore:
	RESTORE_ALL __LC_RETURN_PSW,0
io_done:

#
# There is work todo, find out in which context we have been interrupted:
# 1) if we return to user space we can do all _TIF_WORK_INT work
# 2) if we return to kernel code and kvm is enabled check if we need to
#    modify the psw to leave SIE
# 3) if we return to kernel code and preemptive scheduling is enabled check
#    the preemption counter and if it is zero call preempt_schedule_irq
# Before any work can be done, a switch to the kernel stack is required.
#
io_work:
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	jo	io_work_user		# yes -> do resched & signal
#ifdef CONFIG_PREEMPT
	# check for preemptive scheduling
	icm	%r0,15,__TI_precount(%r12)
	jnz	io_restore		# preemption is disabled
	tm	__TI_flags+7(%r12),_TIF_NEED_RESCHED
	jno	io_restore
	# switch to kernel stack
	lg	%r1,SP_R15(%r15)
	aghi	%r1,-SP_SIZE
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1) # clear back chain
	lgr	%r15,%r1
	# TRACE_IRQS_ON already done at io_return, call
	# TRACE_IRQS_OFF to keep things symmetrical
	TRACE_IRQS_OFF
	brasl	%r14,preempt_schedule_irq
	j	io_return
#else
	j	io_restore
#endif

#
# Need to do work before returning to userspace, switch to kernel stack
#
io_work_user:
	lg	%r1,__LC_KERNEL_STACK
	aghi	%r1,-SP_SIZE
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1) # clear back chain
	lgr	%r15,%r1

#
# One of the work bits is on. Find out which one.
# Checked are: _TIF_SIGPENDING, _TIF_NOTIFY_RESUME, _TIF_NEED_RESCHED
#	       and _TIF_MCCK_PENDING
#
io_work_tif:
	tm	__TI_flags+7(%r12),_TIF_MCCK_PENDING
	jo	io_mcck_pending
	tm	__TI_flags+7(%r12),_TIF_NEED_RESCHED
	jo	io_reschedule
	tm	__TI_flags+7(%r12),_TIF_SIGPENDING
	jo	io_sigpending
	tm	__TI_flags+7(%r12),_TIF_NOTIFY_RESUME
	jo	io_notify_resume
	j	io_return		# beware of critical section cleanup

#
# _TIF_MCCK_PENDING is set, call handler
#
io_mcck_pending:
	# TRACE_IRQS_ON already done at io_return
	brasl	%r14,s390_handle_mcck	# TIF bit will be cleared by handler
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_NEED_RESCHED is set, call schedule
#
io_reschedule:
	# TRACE_IRQS_ON already done at io_return
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	brasl	%r14,schedule		# call scheduler
	stnsm	__SF_EMPTY(%r15),0xfc	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_SIGPENDING or is set, call do_signal
#
io_sigpending:
	# TRACE_IRQS_ON already done at io_return
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	brasl	%r14,do_signal		# call do_signal
	stnsm	__SF_EMPTY(%r15),0xfc	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

#
# _TIF_NOTIFY_RESUME or is set, call do_notify_resume
#
io_notify_resume:
	# TRACE_IRQS_ON already done at io_return
	stosm	__SF_EMPTY(%r15),0x03	# reenable interrupts
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	brasl	%r14,do_notify_resume	# call do_notify_resume
	stnsm	__SF_EMPTY(%r15),0xfc	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	io_return

/*
 * External interrupt handler routine
 */
	.globl	ext_int_handler
ext_int_handler:
	stck	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	SAVE_ALL_ASYNC __LC_EXT_OLD_PSW,__LC_SAVE_AREA+40
	CREATE_STACK_FRAME __LC_SAVE_AREA+40
	mvc	SP_PSW(16,%r15),0(%r12)	# move user PSW to stack
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	jz	ext_no_vtime
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_ASYNC_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_ASYNC_ENTER_TIMER
	LAST_BREAK
ext_no_vtime:
	HANDLE_SIE_INTERCEPT
	TRACE_IRQS_OFF
	la	%r2,SP_PTREGS(%r15)	# address of register-save area
	llgh	%r3,__LC_EXT_INT_CODE	# get interruption code
	brasl	%r14,do_extint
	j	io_return

__critical_end:

/*
 * Machine check handler routines
 */
	.globl mcck_int_handler
mcck_int_handler:
	stck	__LC_MCCK_CLOCK
	la	%r1,4095		# revalidate r1
	spt	__LC_CPU_TIMER_SAVE_AREA-4095(%r1)	# revalidate cpu timer
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)# revalidate gprs
	stmg	%r11,%r15,__LC_SAVE_AREA+80
	larl	%r13,system_call
	lg	%r11,__LC_LAST_BREAK
	la	%r12,__LC_MCK_OLD_PSW
	tm	__LC_MCCK_CODE,0x80	# system damage?
	jo	mcck_int_main		# yes -> rest of mcck code invalid
	la	%r14,4095
	mvc	__LC_MCCK_ENTER_TIMER(8),__LC_CPU_TIMER_SAVE_AREA-4095(%r14)
	tm	__LC_MCCK_CODE+5,0x02	# stored cpu timer value valid?
	jo	1f
	la	%r14,__LC_SYNC_ENTER_TIMER
	clc	0(8,%r14),__LC_ASYNC_ENTER_TIMER
	jl	0f
	la	%r14,__LC_ASYNC_ENTER_TIMER
0:	clc	0(8,%r14),__LC_EXIT_TIMER
	jl	0f
	la	%r14,__LC_EXIT_TIMER
0:	clc	0(8,%r14),__LC_LAST_UPDATE_TIMER
	jl	0f
	la	%r14,__LC_LAST_UPDATE_TIMER
0:	spt	0(%r14)
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
1:	tm	__LC_MCCK_CODE+2,0x09	# mwp + ia of old psw valid?
	jno	mcck_int_main		# no -> skip cleanup critical
	tm	__LC_MCK_OLD_PSW+1,0x01 # test problem state bit
	jnz	mcck_int_main		# from user -> load kernel stack
	clc	__LC_MCK_OLD_PSW+8(8),BASED(.Lcritical_end)
	jhe	mcck_int_main
	clc	__LC_MCK_OLD_PSW+8(8),BASED(.Lcritical_start)
	jl	mcck_int_main
	brasl	%r14,cleanup_critical
mcck_int_main:
	lg	%r14,__LC_PANIC_STACK	# are we already on the panic stack?
	slgr	%r14,%r15
	srag	%r14,%r14,PAGE_SHIFT
	jz	0f
	lg	%r15,__LC_PANIC_STACK	# load panic stack
0:	aghi	%r15,-SP_SIZE		# make room for registers & psw
	CREATE_STACK_FRAME __LC_SAVE_AREA+80
	mvc	SP_PSW(16,%r15),0(%r12)
	lg	%r12,__LC_THREAD_INFO	# load pointer to thread_info struct
	tm	__LC_MCCK_CODE+2,0x08	# mwp of old psw valid?
	jno	mcck_no_vtime		# no -> no timer update
	tm	SP_PSW+1(%r15),0x01	# interrupting from user ?
	jz	mcck_no_vtime
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_MCCK_ENTER_TIMER,__LC_USER_TIMER
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_MCCK_ENTER_TIMER
	LAST_BREAK
mcck_no_vtime:
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	brasl	%r14,s390_do_machine_check
	tm	SP_PSW+1(%r15),0x01	# returning to user ?
	jno	mcck_return
	lg	%r1,__LC_KERNEL_STACK	# switch to kernel stack
	aghi	%r1,-SP_SIZE
	mvc	SP_PTREGS(__PT_SIZE,%r1),SP_PTREGS(%r15)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1) # clear back chain
	lgr	%r15,%r1
	stosm	__SF_EMPTY(%r15),0x04	# turn dat on
	tm	__TI_flags+7(%r12),_TIF_MCCK_PENDING
	jno	mcck_return
	HANDLE_SIE_INTERCEPT
	TRACE_IRQS_OFF
	brasl	%r14,s390_handle_mcck
	TRACE_IRQS_ON
mcck_return:
	mvc	__LC_RETURN_MCCK_PSW(16),SP_PSW(%r15) # move return PSW
	ni	__LC_RETURN_MCCK_PSW+1,0xfd # clear wait state bit
	lmg	%r0,%r15,SP_R0(%r15)	# load gprs 0-15
	tm	__LC_RETURN_MCCK_PSW+1,0x01 # returning to user ?
	jno	0f
	stpt	__LC_EXIT_TIMER
0:	lpswe	__LC_RETURN_MCCK_PSW	# back to caller
mcck_done:

/*
 * Restart interruption handler, kick starter for additional CPUs
 */
#ifdef CONFIG_SMP
	__CPUINIT
	.globl restart_int_handler
restart_int_handler:
	basr	%r1,0
restart_base:
	spt	restart_vtime-restart_base(%r1)
	stck	__LC_LAST_UPDATE_CLOCK
	mvc	__LC_LAST_UPDATE_TIMER(8),restart_vtime-restart_base(%r1)
	mvc	__LC_EXIT_TIMER(8),restart_vtime-restart_base(%r1)
	lg	%r15,__LC_SAVE_AREA+120 # load ksp
	lghi	%r10,__LC_CREGS_SAVE_AREA
	lctlg	%c0,%c15,0(%r10) # get new ctl regs
	lghi	%r10,__LC_AREGS_SAVE_AREA
	lam	%a0,%a15,0(%r10)
	lmg	%r6,%r15,__SF_GPRS(%r15) # load registers from clone
	lg	%r1,__LC_THREAD_INFO
	mvc	__LC_USER_TIMER(8),__TI_user_timer(%r1)
	mvc	__LC_SYSTEM_TIMER(8),__TI_system_timer(%r1)
	xc	__LC_STEAL_TIMER(8),__LC_STEAL_TIMER
	stosm	__SF_EMPTY(%r15),0x04	# now we can turn dat on
	jg	start_secondary
	.align	8
restart_vtime:
	.long	0x7fffffff,0xffffffff
	.previous
#else
/*
 * If we do not run with SMP enabled, let the new CPU crash ...
 */
	.globl restart_int_handler
restart_int_handler:
	basr	%r1,0
restart_base:
	lpswe	restart_crash-restart_base(%r1)
	.align 8
restart_crash:
	.long  0x000a0000,0x00000000,0x00000000,0x00000000
restart_go:
#endif

#ifdef CONFIG_CHECK_STACK
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
stack_overflow:
	lg	%r15,__LC_PANIC_STACK	# change to panic stack
	aghi	%r15,-SP_SIZE
	mvc	SP_PSW(16,%r15),0(%r12)	# move user PSW to stack
	stmg	%r0,%r10,SP_R0(%r15)	# store gprs %r0-%r10 to kernel stack
	la	%r1,__LC_SAVE_AREA
	chi	%r12,__LC_SVC_OLD_PSW
	je	0f
	chi	%r12,__LC_PGM_OLD_PSW
	je	0f
	la	%r1,__LC_SAVE_AREA+40
0:	mvc	SP_R11(40,%r15),0(%r1)	# move %r11-%r15 to stack
	mvc	SP_ARGS(8,%r15),__LC_LAST_BREAK
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15) # clear back chain
	la	%r2,SP_PTREGS(%r15)	# load pt_regs
	jg	kernel_stack_overflow
#endif

cleanup_table_system_call:
	.quad	system_call, sysc_do_svc
cleanup_table_sysc_tif:
	.quad	sysc_tif, sysc_restore
cleanup_table_sysc_restore:
	.quad	sysc_restore, sysc_done
cleanup_table_io_tif:
	.quad	io_tif, io_restore
cleanup_table_io_restore:
	.quad	io_restore, io_done

cleanup_critical:
	clc	8(8,%r12),BASED(cleanup_table_system_call)
	jl	0f
	clc	8(8,%r12),BASED(cleanup_table_system_call+8)
	jl	cleanup_system_call
0:
	clc	8(8,%r12),BASED(cleanup_table_sysc_tif)
	jl	0f
	clc	8(8,%r12),BASED(cleanup_table_sysc_tif+8)
	jl	cleanup_sysc_tif
0:
	clc	8(8,%r12),BASED(cleanup_table_sysc_restore)
	jl	0f
	clc	8(8,%r12),BASED(cleanup_table_sysc_restore+8)
	jl	cleanup_sysc_restore
0:
	clc	8(8,%r12),BASED(cleanup_table_io_tif)
	jl	0f
	clc	8(8,%r12),BASED(cleanup_table_io_tif+8)
	jl	cleanup_io_tif
0:
	clc	8(8,%r12),BASED(cleanup_table_io_restore)
	jl	0f
	clc	8(8,%r12),BASED(cleanup_table_io_restore+8)
	jl	cleanup_io_restore
0:
	br	%r14

cleanup_system_call:
	mvc	__LC_RETURN_PSW(16),0(%r12)
	clc	__LC_RETURN_PSW+8(8),BASED(cleanup_system_call_insn+8)
	jh	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_MCCK_ENTER_TIMER
	cghi	%r12,__LC_MCK_OLD_PSW
	je	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_ASYNC_ENTER_TIMER
0:	cghi	%r12,__LC_MCK_OLD_PSW
	la	%r12,__LC_SAVE_AREA+80
	je	0f
	la	%r12,__LC_SAVE_AREA+40
0:	clc	__LC_RETURN_PSW+8(8),BASED(cleanup_system_call_insn+16)
	jhe	cleanup_vtime
	clc	__LC_RETURN_PSW+8(8),BASED(cleanup_system_call_insn)
	jh	0f
	mvc	__LC_SAVE_AREA(40),0(%r12)
0:	lg	%r15,__LC_KERNEL_STACK	# problem state -> load ksp
	aghi	%r15,-SP_SIZE		# make room for registers & psw
	stg	%r15,32(%r12)
	stg	%r11,0(%r12)
	CREATE_STACK_FRAME __LC_SAVE_AREA
	mvc	SP_PSW(16,%r15),__LC_SVC_OLD_PSW
	mvc	SP_ILC(4,%r15),__LC_SVC_ILC
	stg	%r7,SP_ARGS(%r15)
	mvc	8(8,%r12),__LC_THREAD_INFO
cleanup_vtime:
	clc	__LC_RETURN_PSW+8(8),BASED(cleanup_system_call_insn+24)
	jhe	cleanup_stime
	UPDATE_VTIME __LC_EXIT_TIMER,__LC_SYNC_ENTER_TIMER,__LC_USER_TIMER
cleanup_stime:
	clc	__LC_RETURN_PSW+8(8),BASED(cleanup_system_call_insn+32)
	jh	cleanup_update
	UPDATE_VTIME __LC_LAST_UPDATE_TIMER,__LC_EXIT_TIMER,__LC_SYSTEM_TIMER
cleanup_update:
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	srag	%r12,%r11,23
	lg	%r12,__LC_THREAD_INFO
	jz	0f
	stg	%r11,__TI_last_break(%r12)
0:	mvc	__LC_RETURN_PSW+8(8),BASED(cleanup_table_system_call+8)
	la	%r12,__LC_RETURN_PSW
	br	%r14
cleanup_system_call_insn:
	.quad	sysc_saveall
	.quad	system_call
	.quad	sysc_vtime
	.quad	sysc_stime
	.quad	sysc_update

cleanup_sysc_tif:
	mvc	__LC_RETURN_PSW(8),0(%r12)
	mvc	__LC_RETURN_PSW+8(8),BASED(cleanup_table_sysc_tif)
	la	%r12,__LC_RETURN_PSW
	br	%r14

cleanup_sysc_restore:
	clc	8(8,%r12),BASED(cleanup_sysc_restore_insn)
	je	2f
	clc	8(8,%r12),BASED(cleanup_sysc_restore_insn+8)
	jhe	0f
	mvc	__LC_EXIT_TIMER(8),__LC_MCCK_ENTER_TIMER
	cghi	%r12,__LC_MCK_OLD_PSW
	je	0f
	mvc	__LC_EXIT_TIMER(8),__LC_ASYNC_ENTER_TIMER
0:	mvc	__LC_RETURN_PSW(16),SP_PSW(%r15)
	cghi	%r12,__LC_MCK_OLD_PSW
	la	%r12,__LC_SAVE_AREA+80
	je	1f
	la	%r12,__LC_SAVE_AREA+40
1:	mvc	0(40,%r12),SP_R11(%r15)
	lmg	%r0,%r10,SP_R0(%r15)
	lg	%r15,SP_R15(%r15)
2:	la	%r12,__LC_RETURN_PSW
	br	%r14
cleanup_sysc_restore_insn:
	.quad	sysc_done - 4
	.quad	sysc_done - 16

cleanup_io_tif:
	mvc	__LC_RETURN_PSW(8),0(%r12)
	mvc	__LC_RETURN_PSW+8(8),BASED(cleanup_table_io_tif)
	la	%r12,__LC_RETURN_PSW
	br	%r14

cleanup_io_restore:
	clc	8(8,%r12),BASED(cleanup_io_restore_insn)
	je	1f
	clc	8(8,%r12),BASED(cleanup_io_restore_insn+8)
	jhe	0f
	mvc	__LC_EXIT_TIMER(8),__LC_MCCK_ENTER_TIMER
0:	mvc	__LC_RETURN_PSW(16),SP_PSW(%r15)
	mvc	__LC_SAVE_AREA+80(40),SP_R11(%r15)
	lmg	%r0,%r10,SP_R0(%r15)
	lg	%r15,SP_R15(%r15)
1:	la	%r12,__LC_RETURN_PSW
	br	%r14
cleanup_io_restore_insn:
	.quad	io_done - 4
	.quad	io_done - 16

/*
 * Integer constants
 */
		.align	4
.Lcritical_start:
		.quad	__critical_start
.Lcritical_end:
		.quad	__critical_end

		.section .rodata, "a"
#define SYSCALL(esa,esame,emu)	.long esame
	.globl	sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL

#ifdef CONFIG_COMPAT

#define SYSCALL(esa,esame,emu)	.long emu
sys_call_table_emu:
#include "syscalls.S"
#undef SYSCALL
#endif
