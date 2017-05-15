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
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/errno.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <asm/sigp.h>
#include <asm/irq.h>
#include <asm/vx-insn.h>
#include <asm/setup.h>
#include <asm/nmi.h>
#include <asm/export.h>

__PT_R0      =	__PT_GPRS
__PT_R1      =	__PT_GPRS + 8
__PT_R2      =	__PT_GPRS + 16
__PT_R3      =	__PT_GPRS + 24
__PT_R4      =	__PT_GPRS + 32
__PT_R5      =	__PT_GPRS + 40
__PT_R6      =	__PT_GPRS + 48
__PT_R7      =	__PT_GPRS + 56
__PT_R8      =	__PT_GPRS + 64
__PT_R9      =	__PT_GPRS + 72
__PT_R10     =	__PT_GPRS + 80
__PT_R11     =	__PT_GPRS + 88
__PT_R12     =	__PT_GPRS + 96
__PT_R13     =	__PT_GPRS + 104
__PT_R14     =	__PT_GPRS + 112
__PT_R15     =	__PT_GPRS + 120

STACK_SHIFT = PAGE_SHIFT + THREAD_SIZE_ORDER
STACK_SIZE  = 1 << STACK_SHIFT
STACK_INIT = STACK_SIZE - STACK_FRAME_OVERHEAD - __PT_SIZE

_TIF_WORK	= (_TIF_SIGPENDING | _TIF_NOTIFY_RESUME | _TIF_NEED_RESCHED | \
		   _TIF_UPROBE | _TIF_GUARDED_STORAGE | _TIF_PATCH_PENDING)
_TIF_TRACE	= (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | _TIF_SECCOMP | \
		   _TIF_SYSCALL_TRACEPOINT)
_CIF_WORK	= (_CIF_MCCK_PENDING | _CIF_ASCE_PRIMARY | \
		   _CIF_ASCE_SECONDARY | _CIF_FPU)
_PIF_WORK	= (_PIF_PER_TRAP)

#define BASED(name) name-cleanup_critical(%r13)

	.macro	TRACE_IRQS_ON
#ifdef CONFIG_TRACE_IRQFLAGS
	basr	%r2,%r0
	brasl	%r14,trace_hardirqs_on_caller
#endif
	.endm

	.macro	TRACE_IRQS_OFF
#ifdef CONFIG_TRACE_IRQFLAGS
	basr	%r2,%r0
	brasl	%r14,trace_hardirqs_off_caller
#endif
	.endm

	.macro	LOCKDEP_SYS_EXIT
#ifdef CONFIG_LOCKDEP
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jz	.+10
	brasl	%r14,lockdep_sys_exit
#endif
	.endm

	.macro	CHECK_STACK stacksize,savearea
#ifdef CONFIG_CHECK_STACK
	tml	%r15,\stacksize - CONFIG_STACK_GUARD
	lghi	%r14,\savearea
	jz	stack_overflow
#endif
	.endm

	.macro	SWITCH_ASYNC savearea,timer
	tmhh	%r8,0x0001		# interrupting from user ?
	jnz	1f
	lgr	%r14,%r9
	slg	%r14,BASED(.Lcritical_start)
	clg	%r14,BASED(.Lcritical_length)
	jhe	0f
	lghi	%r11,\savearea		# inside critical section, do cleanup
	brasl	%r14,cleanup_critical
	tmhh	%r8,0x0001		# retest problem state after cleanup
	jnz	1f
0:	lg	%r14,__LC_ASYNC_STACK	# are we already on the async stack?
	slgr	%r14,%r15
	srag	%r14,%r14,STACK_SHIFT
	jnz	2f
	CHECK_STACK 1<<STACK_SHIFT,\savearea
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	3f
1:	UPDATE_VTIME %r14,%r15,\timer
2:	lg	%r15,__LC_ASYNC_STACK	# load async stack
3:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	.endm

	.macro UPDATE_VTIME w1,w2,enter_timer
	lg	\w1,__LC_EXIT_TIMER
	lg	\w2,__LC_LAST_UPDATE_TIMER
	slg	\w1,\enter_timer
	slg	\w2,__LC_EXIT_TIMER
	alg	\w1,__LC_USER_TIMER
	alg	\w2,__LC_SYSTEM_TIMER
	stg	\w1,__LC_USER_TIMER
	stg	\w2,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),\enter_timer
	.endm

	.macro REENABLE_IRQS
	stg	%r8,__LC_RETURN_PSW
	ni	__LC_RETURN_PSW,0xbf
	ssm	__LC_RETURN_PSW
	.endm

	.macro STCK savearea
#ifdef CONFIG_HAVE_MARCH_Z9_109_FEATURES
	.insn	s,0xb27c0000,\savearea		# store clock fast
#else
	.insn	s,0xb2050000,\savearea		# store clock
#endif
	.endm

	/*
	 * The TSTMSK macro generates a test-under-mask instruction by
	 * calculating the memory offset for the specified mask value.
	 * Mask value can be any constant.  The macro shifts the mask
	 * value to calculate the memory offset for the test-under-mask
	 * instruction.
	 */
	.macro TSTMSK addr, mask, size=8, bytepos=0
		.if (\bytepos < \size) && (\mask >> 8)
			.if (\mask & 0xff)
				.error "Mask exceeds byte boundary"
			.endif
			TSTMSK \addr, "(\mask >> 8)", \size, "(\bytepos + 1)"
			.exitm
		.endif
		.ifeq \mask
			.error "Mask must not be zero"
		.endif
		off = \size - \bytepos - 1
		tm	off+\addr, \mask
	.endm

	.section .kprobes.text, "ax"
.Ldummy:
	/*
	 * This nop exists only in order to avoid that __switch_to starts at
	 * the beginning of the kprobes text section. In that case we would
	 * have several symbols at the same address. E.g. objdump would take
	 * an arbitrary symbol name when disassembling this code.
	 * With the added nop in between the __switch_to symbol is unique
	 * again.
	 */
	nop	0

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
ENTRY(__switch_to)
	stmg	%r6,%r15,__SF_GPRS(%r15)	# store gprs of prev task
	lgr	%r1,%r2
	aghi	%r1,__TASK_thread		# thread_struct of prev task
	lg	%r5,__TASK_stack(%r3)		# start of kernel stack of next
	stg	%r15,__THREAD_ksp(%r1)		# store kernel stack of prev
	lgr	%r1,%r3
	aghi	%r1,__TASK_thread		# thread_struct of next task
	lgr	%r15,%r5
	aghi	%r15,STACK_INIT			# end of kernel stack of next
	stg	%r3,__LC_CURRENT		# store task struct of next
	stg	%r15,__LC_KERNEL_STACK		# store end of kernel stack
	lg	%r15,__THREAD_ksp(%r1)		# load kernel stack of next
	mvc	__LC_CURRENT_PID(4,%r0),__TASK_pid(%r3) # store pid of next
	lmg	%r6,%r15,__SF_GPRS(%r15)	# load gprs of next task
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_LPP
	bzr	%r14
	.insn	s,0xb2800000,__LC_LPP		# set program parameter
	br	%r14

.L__critical_start:

#if IS_ENABLED(CONFIG_KVM)
/*
 * sie64a calling convention:
 * %r2 pointer to sie control block
 * %r3 guest register save area
 */
ENTRY(sie64a)
	stmg	%r6,%r14,__SF_GPRS(%r15)	# save kernel registers
	stg	%r2,__SF_EMPTY(%r15)		# save control block pointer
	stg	%r3,__SF_EMPTY+8(%r15)		# save guest register save area
	xc	__SF_EMPTY+16(8,%r15),__SF_EMPTY+16(%r15) # reason code = 0
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU		# load guest fp/vx registers ?
	jno	.Lsie_load_guest_gprs
	brasl	%r14,load_fpu_regs		# load guest fp/vx regs
.Lsie_load_guest_gprs:
	lmg	%r0,%r13,0(%r3)			# load guest gprs 0-13
	lg	%r14,__LC_GMAP			# get gmap pointer
	ltgr	%r14,%r14
	jz	.Lsie_gmap
	lctlg	%c1,%c1,__GMAP_ASCE(%r14)	# load primary asce
.Lsie_gmap:
	lg	%r14,__SF_EMPTY(%r15)		# get control block pointer
	oi	__SIE_PROG0C+3(%r14),1		# we are going into SIE now
	tm	__SIE_PROG20+3(%r14),3		# last exit...
	jnz	.Lsie_skip
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lsie_skip			# exit if fp/vx regs changed
	sie	0(%r14)
.Lsie_skip:
	ni	__SIE_PROG0C+3(%r14),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
.Lsie_done:
# some program checks are suppressing. C code (e.g. do_protection_exception)
# will rewind the PSW by the ILC, which is often 4 bytes in case of SIE. There
# are some corner cases (e.g. runtime instrumentation) where ILC is unpredictable.
# Other instructions between sie64a and .Lsie_done should not cause program
# interrupts. So lets use 3 nops as a landing pad for all possible rewinds.
# See also .Lcleanup_sie
.Lrewind_pad6:
	nopr	7
.Lrewind_pad4:
	nopr	7
.Lrewind_pad2:
	nopr	7
	.globl sie_exit
sie_exit:
	lg	%r14,__SF_EMPTY+8(%r15)		# load guest register save area
	stmg	%r0,%r13,0(%r14)		# save guest gprs 0-13
	lmg	%r6,%r14,__SF_GPRS(%r15)	# restore kernel registers
	lg	%r2,__SF_EMPTY+16(%r15)		# return exit reason code
	br	%r14
.Lsie_fault:
	lghi	%r14,-EFAULT
	stg	%r14,__SF_EMPTY+16(%r15)	# set exit reason code
	j	sie_exit

	EX_TABLE(.Lrewind_pad6,.Lsie_fault)
	EX_TABLE(.Lrewind_pad4,.Lsie_fault)
	EX_TABLE(.Lrewind_pad2,.Lsie_fault)
	EX_TABLE(sie_exit,.Lsie_fault)
EXPORT_SYMBOL(sie64a)
EXPORT_SYMBOL(sie_exit)
#endif

/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are executed with interrupts enabled.
 */

ENTRY(system_call)
	stpt	__LC_SYNC_ENTER_TIMER
.Lsysc_stmg:
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	lg	%r12,__LC_CURRENT
	lghi	%r13,__TASK_thread
	lghi	%r14,_PIF_SYSCALL
.Lsysc_per:
	lg	%r15,__LC_KERNEL_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)	# pointer to pt_regs
.Lsysc_vtime:
	UPDATE_VTIME %r8,%r9,__LC_SYNC_ENTER_TIMER
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	mvc	__PT_PSW(16,%r11),__LC_SVC_OLD_PSW
	mvc	__PT_INT_CODE(4,%r11),__LC_SVC_ILC
	stg	%r14,__PT_FLAGS(%r11)
.Lsysc_do_svc:
	# load address of system call table
	lg	%r10,__THREAD_sysc_table(%r13,%r12)
	llgh	%r8,__PT_INT_CODE+2(%r11)
	slag	%r8,%r8,2			# shift and test for svc 0
	jnz	.Lsysc_nr_ok
	# svc 0: system call number in %r1
	llgfr	%r1,%r1				# clear high word in r1
	cghi	%r1,NR_syscalls
	jnl	.Lsysc_nr_ok
	sth	%r1,__PT_INT_CODE+2(%r11)
	slag	%r8,%r1,2
.Lsysc_nr_ok:
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stg	%r2,__PT_ORIG_GPR2(%r11)
	stg	%r7,STACK_FRAME_OVERHEAD(%r15)
	lgf	%r9,0(%r8,%r10)			# get system call add.
	TSTMSK	__TI_flags(%r12),_TIF_TRACE
	jnz	.Lsysc_tracesys
	basr	%r14,%r9			# call sys_xxxx
	stg	%r2,__PT_R2(%r11)		# store return value

.Lsysc_return:
	LOCKDEP_SYS_EXIT
.Lsysc_tif:
	TSTMSK	__PT_FLAGS(%r11),_PIF_WORK
	jnz	.Lsysc_work
	TSTMSK	__TI_flags(%r12),_TIF_WORK
	jnz	.Lsysc_work			# check for work
	TSTMSK	__LC_CPU_FLAGS,_CIF_WORK
	jnz	.Lsysc_work
.Lsysc_restore:
	lg	%r14,__LC_VDSO_PER_CPU
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r11)
.Lsysc_exit_timer:
	stpt	__LC_EXIT_TIMER
	mvc	__VDSO_ECTG_BASE(16,%r14),__LC_EXIT_TIMER
	lmg	%r11,%r15,__PT_R11(%r11)
	lpswe	__LC_RETURN_PSW
.Lsysc_done:

#
# One of the work bits is on. Find out which one.
#
.Lsysc_work:
	TSTMSK	__LC_CPU_FLAGS,_CIF_MCCK_PENDING
	jo	.Lsysc_mcck_pending
	TSTMSK	__TI_flags(%r12),_TIF_NEED_RESCHED
	jo	.Lsysc_reschedule
#ifdef CONFIG_UPROBES
	TSTMSK	__TI_flags(%r12),_TIF_UPROBE
	jo	.Lsysc_uprobe_notify
#endif
	TSTMSK	__TI_flags(%r12),_TIF_GUARDED_STORAGE
	jo	.Lsysc_guarded_storage
	TSTMSK	__PT_FLAGS(%r11),_PIF_PER_TRAP
	jo	.Lsysc_singlestep
#ifdef CONFIG_LIVEPATCH
	TSTMSK	__TI_flags(%r12),_TIF_PATCH_PENDING
	jo	.Lsysc_patch_pending	# handle live patching just before
					# signals and possible syscall restart
#endif
	TSTMSK	__TI_flags(%r12),_TIF_SIGPENDING
	jo	.Lsysc_sigpending
	TSTMSK	__TI_flags(%r12),_TIF_NOTIFY_RESUME
	jo	.Lsysc_notify_resume
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lsysc_vxrs
	TSTMSK	__LC_CPU_FLAGS,(_CIF_ASCE_PRIMARY|_CIF_ASCE_SECONDARY)
	jnz	.Lsysc_asce
	j	.Lsysc_return		# beware of critical section cleanup

#
# _TIF_NEED_RESCHED is set, call schedule
#
.Lsysc_reschedule:
	larl	%r14,.Lsysc_return
	jg	schedule

#
# _CIF_MCCK_PENDING is set, call handler
#
.Lsysc_mcck_pending:
	larl	%r14,.Lsysc_return
	jg	s390_handle_mcck	# TIF bit will be cleared by handler

#
# _CIF_ASCE_PRIMARY and/or CIF_ASCE_SECONDARY set, load user space asce
#
.Lsysc_asce:
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_PRIMARY
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	TSTMSK	__LC_CPU_FLAGS,_CIF_ASCE_SECONDARY
	jz	.Lsysc_return
	larl	%r14,.Lsysc_return
	jg	set_fs_fixup

#
# CIF_FPU is set, restore floating-point controls and floating-point registers.
#
.Lsysc_vxrs:
	larl	%r14,.Lsysc_return
	jg	load_fpu_regs

#
# _TIF_SIGPENDING is set, call do_signal
#
.Lsysc_sigpending:
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_signal
	TSTMSK	__PT_FLAGS(%r11),_PIF_SYSCALL
	jno	.Lsysc_return
.Lsysc_do_syscall:
	lghi	%r13,__TASK_thread
	lmg	%r2,%r7,__PT_R2(%r11)	# load svc arguments
	lghi	%r1,0			# svc 0 returns -ENOSYS
	j	.Lsysc_do_svc

#
# _TIF_NOTIFY_RESUME is set, call do_notify_resume
#
.Lsysc_notify_resume:
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	do_notify_resume

#
# _TIF_UPROBE is set, call uprobe_notify_resume
#
#ifdef CONFIG_UPROBES
.Lsysc_uprobe_notify:
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	uprobe_notify_resume
#endif

#
# _TIF_GUARDED_STORAGE is set, call guarded_storage_load
#
.Lsysc_guarded_storage:
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	gs_load_bc_cb
#
# _TIF_PATCH_PENDING is set, call klp_update_patch_state
#
#ifdef CONFIG_LIVEPATCH
.Lsysc_patch_pending:
	lg	%r2,__LC_CURRENT	# pass pointer to task struct
	larl	%r14,.Lsysc_return
	jg	klp_update_patch_state
#endif

#
# _PIF_PER_TRAP is set, call do_per_trap
#
.Lsysc_singlestep:
	ni	__PT_FLAGS+7(%r11),255-_PIF_PER_TRAP
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	do_per_trap

#
# call tracehook_report_syscall_entry/tracehook_report_syscall_exit before
# and after the system call
#
.Lsysc_tracesys:
	lgr	%r2,%r11		# pass pointer to pt_regs
	la	%r3,0
	llgh	%r0,__PT_INT_CODE+2(%r11)
	stg	%r0,__PT_R2(%r11)
	brasl	%r14,do_syscall_trace_enter
	lghi	%r0,NR_syscalls
	clgr	%r0,%r2
	jnh	.Lsysc_tracenogo
	sllg	%r8,%r2,2
	lgf	%r9,0(%r8,%r10)
.Lsysc_tracego:
	lmg	%r3,%r7,__PT_R3(%r11)
	stg	%r7,STACK_FRAME_OVERHEAD(%r15)
	lg	%r2,__PT_ORIG_GPR2(%r11)
	basr	%r14,%r9		# call sys_xxx
	stg	%r2,__PT_R2(%r11)	# store return value
.Lsysc_tracenogo:
	TSTMSK	__TI_flags(%r12),_TIF_TRACE
	jz	.Lsysc_return
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	do_syscall_trace_exit

#
# a new process exits the kernel with ret_from_fork
#
ENTRY(ret_from_fork)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	lg	%r12,__LC_CURRENT
	brasl	%r14,schedule_tail
	TRACE_IRQS_ON
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	tm	__PT_PSW+1(%r11),0x01	# forking a kernel thread ?
	jne	.Lsysc_tracenogo
	# it's a kernel thread
	lmg	%r9,%r10,__PT_R9(%r11)	# load gprs
ENTRY(kernel_thread_starter)
	la	%r2,0(%r10)
	basr	%r14,%r9
	j	.Lsysc_tracenogo

/*
 * Program check handler routine
 */

ENTRY(pgm_check_handler)
	stpt	__LC_SYNC_ENTER_TIMER
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	lg	%r10,__LC_LAST_BREAK
	lg	%r12,__LC_CURRENT
	larl	%r13,cleanup_critical
	lmg	%r8,%r9,__LC_PGM_OLD_PSW
	tmhh	%r8,0x0001		# test problem state bit
	jnz	2f			# -> fault in user space
#if IS_ENABLED(CONFIG_KVM)
	# cleanup critical section for sie64a
	lgr	%r14,%r9
	slg	%r14,BASED(.Lsie_critical_start)
	clg	%r14,BASED(.Lsie_critical_length)
	jhe	0f
	brasl	%r14,.Lcleanup_sie
#endif
0:	tmhh	%r8,0x4000		# PER bit set in old PSW ?
	jnz	1f			# -> enabled, can't be a double fault
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jnz	.Lpgm_svcper		# -> single stepped svc
1:	CHECK_STACK STACK_SIZE,__LC_SAVE_AREA_SYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	4f
2:	UPDATE_VTIME %r14,%r15,__LC_SYNC_ENTER_TIMER
	lg	%r15,__LC_KERNEL_STACK
	lgr	%r14,%r12
	aghi	%r14,__TASK_thread	# pointer to thread_struct
	lghi	%r13,__LC_PGM_TDB
	tm	__LC_PGM_ILC+2,0x02	# check for transaction abort
	jz	3f
	mvc	__THREAD_trap_tdb(256,%r14),0(%r13)
3:	stg	%r10,__THREAD_last_break(%r14)
4:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_INT_CODE(4,%r11),__LC_PGM_ILC
	mvc	__PT_INT_PARM_LONG(8,%r11),__LC_TRANS_EXC_CODE
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	stg	%r10,__PT_ARGS(%r11)
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jz	5f
	tmhh	%r8,0x0001		# kernel per event ?
	jz	.Lpgm_kprobe
	oi	__PT_FLAGS+7(%r11),_PIF_PER_TRAP
	mvc	__THREAD_per_address(8,%r14),__LC_PER_ADDRESS
	mvc	__THREAD_per_cause(2,%r14),__LC_PER_CODE
	mvc	__THREAD_per_paid(1,%r14),__LC_PER_ACCESS_ID
5:	REENABLE_IRQS
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	larl	%r1,pgm_check_table
	llgh	%r10,__PT_INT_CODE+2(%r11)
	nill	%r10,0x007f
	sll	%r10,2
	je	.Lpgm_return
	lgf	%r1,0(%r10,%r1)		# load address of handler routine
	lgr	%r2,%r11		# pass pointer to pt_regs
	basr	%r14,%r1		# branch to interrupt-handler
.Lpgm_return:
	LOCKDEP_SYS_EXIT
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jno	.Lsysc_restore
	TSTMSK	__PT_FLAGS(%r11),_PIF_SYSCALL
	jo	.Lsysc_do_syscall
	j	.Lsysc_tif

#
# PER event in supervisor state, must be kprobes
#
.Lpgm_kprobe:
	REENABLE_IRQS
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_per_trap
	j	.Lpgm_return

#
# single stepped system call
#
.Lpgm_svcper:
	mvc	__LC_RETURN_PSW(8),__LC_SVC_NEW_PSW
	lghi	%r13,__TASK_thread
	larl	%r14,.Lsysc_per
	stg	%r14,__LC_RETURN_PSW+8
	lghi	%r14,_PIF_SYSCALL | _PIF_PER_TRAP
	lpswe	__LC_RETURN_PSW		# branch to .Lsysc_per and enable irqs

/*
 * IO interrupt handler routine
 */
ENTRY(io_int_handler)
	STCK	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	stmg	%r8,%r15,__LC_SAVE_AREA_ASYNC
	lg	%r12,__LC_CURRENT
	larl	%r13,cleanup_critical
	lmg	%r8,%r9,__LC_IO_OLD_PSW
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_ENTER_TIMER
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_ASYNC
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_INT_CODE(12,%r11),__LC_SUBCHANNEL_ID
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	TSTMSK	__LC_CPU_FLAGS,_CIF_IGNORE_IRQ
	jo	.Lio_restore
	TRACE_IRQS_OFF
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
.Lio_loop:
	lgr	%r2,%r11		# pass pointer to pt_regs
	lghi	%r3,IO_INTERRUPT
	tm	__PT_INT_CODE+8(%r11),0x80	# adapter interrupt ?
	jz	.Lio_call
	lghi	%r3,THIN_INTERRUPT
.Lio_call:
	brasl	%r14,do_IRQ
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_LPAR
	jz	.Lio_return
	tpi	0
	jz	.Lio_return
	mvc	__PT_INT_CODE(12,%r11),__LC_SUBCHANNEL_ID
	j	.Lio_loop
.Lio_return:
	LOCKDEP_SYS_EXIT
	TRACE_IRQS_ON
.Lio_tif:
	TSTMSK	__TI_flags(%r12),_TIF_WORK
	jnz	.Lio_work		# there is work to do (signals etc.)
	TSTMSK	__LC_CPU_FLAGS,_CIF_WORK
	jnz	.Lio_work
.Lio_restore:
	lg	%r14,__LC_VDSO_PER_CPU
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r11)
.Lio_exit_timer:
	stpt	__LC_EXIT_TIMER
	mvc	__VDSO_ECTG_BASE(16,%r14),__LC_EXIT_TIMER
	lmg	%r11,%r15,__PT_R11(%r11)
	lpswe	__LC_RETURN_PSW
.Lio_done:

#
# There is work todo, find out in which context we have been interrupted:
# 1) if we return to user space we can do all _TIF_WORK work
# 2) if we return to kernel code and kvm is enabled check if we need to
#    modify the psw to leave SIE
# 3) if we return to kernel code and preemptive scheduling is enabled check
#    the preemption counter and if it is zero call preempt_schedule_irq
# Before any work can be done, a switch to the kernel stack is required.
#
.Lio_work:
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jo	.Lio_work_user		# yes -> do resched & signal
#ifdef CONFIG_PREEMPT
	# check for preemptive scheduling
	icm	%r0,15,__LC_PREEMPT_COUNT
	jnz	.Lio_restore		# preemption is disabled
	TSTMSK	__TI_flags(%r12),_TIF_NEED_RESCHED
	jno	.Lio_restore
	# switch to kernel stack
	lg	%r1,__PT_R15(%r11)
	aghi	%r1,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lgr	%r15,%r1
	# TRACE_IRQS_ON already done at .Lio_return, call
	# TRACE_IRQS_OFF to keep things symmetrical
	TRACE_IRQS_OFF
	brasl	%r14,preempt_schedule_irq
	j	.Lio_return
#else
	j	.Lio_restore
#endif

#
# Need to do work before returning to userspace, switch to kernel stack
#
.Lio_work_user:
	lg	%r1,__LC_KERNEL_STACK
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lgr	%r15,%r1

#
# One of the work bits is on. Find out which one.
#
.Lio_work_tif:
	TSTMSK	__LC_CPU_FLAGS,_CIF_MCCK_PENDING
	jo	.Lio_mcck_pending
	TSTMSK	__TI_flags(%r12),_TIF_NEED_RESCHED
	jo	.Lio_reschedule
#ifdef CONFIG_LIVEPATCH
	TSTMSK	__TI_flags(%r12),_TIF_PATCH_PENDING
	jo	.Lio_patch_pending
#endif
	TSTMSK	__TI_flags(%r12),_TIF_SIGPENDING
	jo	.Lio_sigpending
	TSTMSK	__TI_flags(%r12),_TIF_NOTIFY_RESUME
	jo	.Lio_notify_resume
	TSTMSK	__TI_flags(%r12),_TIF_GUARDED_STORAGE
	jo	.Lio_guarded_storage
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lio_vxrs
	TSTMSK	__LC_CPU_FLAGS,(_CIF_ASCE_PRIMARY|_CIF_ASCE_SECONDARY)
	jnz	.Lio_asce
	j	.Lio_return		# beware of critical section cleanup

#
# _CIF_MCCK_PENDING is set, call handler
#
.Lio_mcck_pending:
	# TRACE_IRQS_ON already done at .Lio_return
	brasl	%r14,s390_handle_mcck	# TIF bit will be cleared by handler
	TRACE_IRQS_OFF
	j	.Lio_return

#
# _CIF_ASCE_PRIMARY and/or CIF_ASCE_SECONDARY set, load user space asce
#
.Lio_asce:
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_PRIMARY
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	TSTMSK	__LC_CPU_FLAGS,_CIF_ASCE_SECONDARY
	jz	.Lio_return
	larl	%r14,.Lio_return
	jg	set_fs_fixup

#
# CIF_FPU is set, restore floating-point controls and floating-point registers.
#
.Lio_vxrs:
	larl	%r14,.Lio_return
	jg	load_fpu_regs

#
# _TIF_GUARDED_STORAGE is set, call guarded_storage_load
#
.Lio_guarded_storage:
	# TRACE_IRQS_ON already done at .Lio_return
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,gs_load_bc_cb
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	.Lio_return

#
# _TIF_NEED_RESCHED is set, call schedule
#
.Lio_reschedule:
	# TRACE_IRQS_ON already done at .Lio_return
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	brasl	%r14,schedule		# call scheduler
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	.Lio_return

#
# _TIF_PATCH_PENDING is set, call klp_update_patch_state
#
#ifdef CONFIG_LIVEPATCH
.Lio_patch_pending:
	lg	%r2,__LC_CURRENT	# pass pointer to task struct
	larl	%r14,.Lio_return
	jg	klp_update_patch_state
#endif

#
# _TIF_SIGPENDING or is set, call do_signal
#
.Lio_sigpending:
	# TRACE_IRQS_ON already done at .Lio_return
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_signal
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	.Lio_return

#
# _TIF_NOTIFY_RESUME or is set, call do_notify_resume
#
.Lio_notify_resume:
	# TRACE_IRQS_ON already done at .Lio_return
	ssm	__LC_SVC_NEW_PSW	# reenable interrupts
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_notify_resume
	ssm	__LC_PGM_NEW_PSW	# disable I/O and ext. interrupts
	TRACE_IRQS_OFF
	j	.Lio_return

/*
 * External interrupt handler routine
 */
ENTRY(ext_int_handler)
	STCK	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	stmg	%r8,%r15,__LC_SAVE_AREA_ASYNC
	lg	%r12,__LC_CURRENT
	larl	%r13,cleanup_critical
	lmg	%r8,%r9,__LC_EXT_OLD_PSW
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_ENTER_TIMER
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_ASYNC
	stmg	%r8,%r9,__PT_PSW(%r11)
	lghi	%r1,__LC_EXT_PARAMS2
	mvc	__PT_INT_CODE(4,%r11),__LC_EXT_CPU_ADDR
	mvc	__PT_INT_PARM(4,%r11),__LC_EXT_PARAMS
	mvc	__PT_INT_PARM_LONG(8,%r11),0(%r1)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	TSTMSK	__LC_CPU_FLAGS,_CIF_IGNORE_IRQ
	jo	.Lio_restore
	TRACE_IRQS_OFF
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	lghi	%r3,EXT_INTERRUPT
	brasl	%r14,do_IRQ
	j	.Lio_return

/*
 * Load idle PSW. The second "half" of this function is in .Lcleanup_idle.
 */
ENTRY(psw_idle)
	stg	%r3,__SF_EMPTY(%r15)
	larl	%r1,.Lpsw_idle_lpsw+4
	stg	%r1,__SF_EMPTY+8(%r15)
#ifdef CONFIG_SMP
	larl	%r1,smp_cpu_mtid
	llgf	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	.Lpsw_idle_stcctm
	.insn	rsy,0xeb0000000017,%r1,5,__SF_EMPTY+16(%r15)
.Lpsw_idle_stcctm:
#endif
	oi	__LC_CPU_FLAGS+7,_CIF_ENABLED_WAIT
	STCK	__CLOCK_IDLE_ENTER(%r2)
	stpt	__TIMER_IDLE_ENTER(%r2)
.Lpsw_idle_lpsw:
	lpswe	__SF_EMPTY(%r15)
	br	%r14
.Lpsw_idle_end:

/*
 * Store floating-point controls and floating-point or vector register
 * depending whether the vector facility is available.	A critical section
 * cleanup assures that the registers are stored even if interrupted for
 * some other work.  The CIF_FPU flag is set to trigger a lazy restore
 * of the register contents at return from io or a system call.
 */
ENTRY(save_fpu_regs)
	lg	%r2,__LC_CURRENT
	aghi	%r2,__TASK_thread
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	bor	%r14
	stfpc	__THREAD_FPU_fpc(%r2)
	lg	%r3,__THREAD_FPU_regs(%r2)
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_VX
	jz	.Lsave_fpu_regs_fp	  # no -> store FP regs
	VSTM	%v0,%v15,0,%r3		  # vstm 0,15,0(3)
	VSTM	%v16,%v31,256,%r3	  # vstm 16,31,256(3)
	j	.Lsave_fpu_regs_done	  # -> set CIF_FPU flag
.Lsave_fpu_regs_fp:
	std	0,0(%r3)
	std	1,8(%r3)
	std	2,16(%r3)
	std	3,24(%r3)
	std	4,32(%r3)
	std	5,40(%r3)
	std	6,48(%r3)
	std	7,56(%r3)
	std	8,64(%r3)
	std	9,72(%r3)
	std	10,80(%r3)
	std	11,88(%r3)
	std	12,96(%r3)
	std	13,104(%r3)
	std	14,112(%r3)
	std	15,120(%r3)
.Lsave_fpu_regs_done:
	oi	__LC_CPU_FLAGS+7,_CIF_FPU
	br	%r14
.Lsave_fpu_regs_end:
#if IS_ENABLED(CONFIG_KVM)
EXPORT_SYMBOL(save_fpu_regs)
#endif

/*
 * Load floating-point controls and floating-point or vector registers.
 * A critical section cleanup assures that the register contents are
 * loaded even if interrupted for some other work.
 *
 * There are special calling conventions to fit into sysc and io return work:
 *	%r15:	<kernel stack>
 * The function requires:
 *	%r4
 */
load_fpu_regs:
	lg	%r4,__LC_CURRENT
	aghi	%r4,__TASK_thread
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	bnor	%r14
	lfpc	__THREAD_FPU_fpc(%r4)
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_VX
	lg	%r4,__THREAD_FPU_regs(%r4)	# %r4 <- reg save area
	jz	.Lload_fpu_regs_fp		# -> no VX, load FP regs
	VLM	%v0,%v15,0,%r4
	VLM	%v16,%v31,256,%r4
	j	.Lload_fpu_regs_done
.Lload_fpu_regs_fp:
	ld	0,0(%r4)
	ld	1,8(%r4)
	ld	2,16(%r4)
	ld	3,24(%r4)
	ld	4,32(%r4)
	ld	5,40(%r4)
	ld	6,48(%r4)
	ld	7,56(%r4)
	ld	8,64(%r4)
	ld	9,72(%r4)
	ld	10,80(%r4)
	ld	11,88(%r4)
	ld	12,96(%r4)
	ld	13,104(%r4)
	ld	14,112(%r4)
	ld	15,120(%r4)
.Lload_fpu_regs_done:
	ni	__LC_CPU_FLAGS+7,255-_CIF_FPU
	br	%r14
.Lload_fpu_regs_end:

.L__critical_end:

/*
 * Machine check handler routines
 */
ENTRY(mcck_int_handler)
	STCK	__LC_MCCK_CLOCK
	la	%r1,4095		# revalidate r1
	spt	__LC_CPU_TIMER_SAVE_AREA-4095(%r1)	# revalidate cpu timer
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)# revalidate gprs
	lg	%r12,__LC_CURRENT
	larl	%r13,cleanup_critical
	lmg	%r8,%r9,__LC_MCK_OLD_PSW
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_SYSTEM_DAMAGE
	jo	.Lmcck_panic		# yes -> rest of mcck code invalid
	lghi	%r14,__LC_CPU_TIMER_SAVE_AREA
	mvc	__LC_MCCK_ENTER_TIMER(8),0(%r14)
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_CPU_TIMER_VALID
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
3:	TSTMSK	__LC_MCCK_CODE,(MCCK_CODE_PSW_MWP_VALID|MCCK_CODE_PSW_IA_VALID)
	jno	.Lmcck_panic		# no -> skip cleanup critical
	SWITCH_ASYNC __LC_GPREGS_SAVE_AREA+64,__LC_MCCK_ENTER_TIMER
.Lmcck_skip:
	lghi	%r14,__LC_GPREGS_SAVE_AREA+64
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),0(%r14)
	stmg	%r8,%r9,__PT_PSW(%r11)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,s390_do_machine_check
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jno	.Lmcck_return
	lg	%r1,__LC_KERNEL_STACK	# switch to kernel stack
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lgr	%r15,%r1
	ssm	__LC_PGM_NEW_PSW	# turn dat on, keep irqs off
	TSTMSK	__LC_CPU_FLAGS,_CIF_MCCK_PENDING
	jno	.Lmcck_return
	TRACE_IRQS_OFF
	brasl	%r14,s390_handle_mcck
	TRACE_IRQS_ON
.Lmcck_return:
	lg	%r14,__LC_VDSO_PER_CPU
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_MCCK_PSW(16),__PT_PSW(%r11) # move return PSW
	tm	__LC_RETURN_MCCK_PSW+1,0x01 # returning to user ?
	jno	0f
	stpt	__LC_EXIT_TIMER
	mvc	__VDSO_ECTG_BASE(16,%r14),__LC_EXIT_TIMER
0:	lmg	%r11,%r15,__PT_R11(%r11)
	lpswe	__LC_RETURN_MCCK_PSW

.Lmcck_panic:
	lg	%r15,__LC_PANIC_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	j	.Lmcck_skip

#
# PSW restart interrupt handler
#
ENTRY(restart_int_handler)
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_LPP
	jz	0f
	.insn	s,0xb2800000,__LC_LPP
0:	stg	%r15,__LC_SAVE_AREA_RESTART
	lg	%r15,__LC_RESTART_STACK
	aghi	%r15,-__PT_SIZE			# create pt_regs on stack
	xc	0(__PT_SIZE,%r15),0(%r15)
	stmg	%r0,%r14,__PT_R0(%r15)
	mvc	__PT_R15(8,%r15),__LC_SAVE_AREA_RESTART
	mvc	__PT_PSW(16,%r15),__LC_RST_OLD_PSW # store restart old psw
	aghi	%r15,-STACK_FRAME_OVERHEAD	# create stack frame on stack
	xc	0(STACK_FRAME_OVERHEAD,%r15),0(%r15)
	lg	%r1,__LC_RESTART_FN		# load fn, parm & source cpu
	lg	%r2,__LC_RESTART_DATA
	lg	%r3,__LC_RESTART_SOURCE
	ltgr	%r3,%r3				# test source cpu address
	jm	1f				# negative -> skip source stop
0:	sigp	%r4,%r3,SIGP_SENSE		# sigp sense to source cpu
	brc	10,0b				# wait for status stored
1:	basr	%r14,%r1			# call function
	stap	__SF_EMPTY(%r15)		# store cpu address
	llgh	%r3,__SF_EMPTY(%r15)
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
	lg	%r15,__LC_PANIC_STACK	# change to panic stack
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_R8(64,%r11),0(%r14)
	stg	%r10,__PT_ORIG_GPR2(%r11) # store last break to orig_gpr2
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	jg	kernel_stack_overflow
#endif

cleanup_critical:
#if IS_ENABLED(CONFIG_KVM)
	clg	%r9,BASED(.Lcleanup_table_sie)	# .Lsie_gmap
	jl	0f
	clg	%r9,BASED(.Lcleanup_table_sie+8)# .Lsie_done
	jl	.Lcleanup_sie
#endif
	clg	%r9,BASED(.Lcleanup_table)	# system_call
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+8)	# .Lsysc_do_svc
	jl	.Lcleanup_system_call
	clg	%r9,BASED(.Lcleanup_table+16)	# .Lsysc_tif
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+24)	# .Lsysc_restore
	jl	.Lcleanup_sysc_tif
	clg	%r9,BASED(.Lcleanup_table+32)	# .Lsysc_done
	jl	.Lcleanup_sysc_restore
	clg	%r9,BASED(.Lcleanup_table+40)	# .Lio_tif
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+48)	# .Lio_restore
	jl	.Lcleanup_io_tif
	clg	%r9,BASED(.Lcleanup_table+56)	# .Lio_done
	jl	.Lcleanup_io_restore
	clg	%r9,BASED(.Lcleanup_table+64)	# psw_idle
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+72)	# .Lpsw_idle_end
	jl	.Lcleanup_idle
	clg	%r9,BASED(.Lcleanup_table+80)	# save_fpu_regs
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+88)	# .Lsave_fpu_regs_end
	jl	.Lcleanup_save_fpu_regs
	clg	%r9,BASED(.Lcleanup_table+96)	# load_fpu_regs
	jl	0f
	clg	%r9,BASED(.Lcleanup_table+104)	# .Lload_fpu_regs_end
	jl	.Lcleanup_load_fpu_regs
0:	br	%r14

	.align	8
.Lcleanup_table:
	.quad	system_call
	.quad	.Lsysc_do_svc
	.quad	.Lsysc_tif
	.quad	.Lsysc_restore
	.quad	.Lsysc_done
	.quad	.Lio_tif
	.quad	.Lio_restore
	.quad	.Lio_done
	.quad	psw_idle
	.quad	.Lpsw_idle_end
	.quad	save_fpu_regs
	.quad	.Lsave_fpu_regs_end
	.quad	load_fpu_regs
	.quad	.Lload_fpu_regs_end

#if IS_ENABLED(CONFIG_KVM)
.Lcleanup_table_sie:
	.quad	.Lsie_gmap
	.quad	.Lsie_done

.Lcleanup_sie:
	lg	%r9,__SF_EMPTY(%r15)		# get control block pointer
	ni	__SIE_PROG0C+3(%r9),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	larl	%r9,sie_exit			# skip forward to sie_exit
	br	%r14
#endif

.Lcleanup_system_call:
	# check if stpt has been executed
	clg	%r9,BASED(.Lcleanup_system_call_insn)
	jh	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_ASYNC_ENTER_TIMER
	cghi	%r11,__LC_SAVE_AREA_ASYNC
	je	0f
	mvc	__LC_SYNC_ENTER_TIMER(8),__LC_MCCK_ENTER_TIMER
0:	# check if stmg has been executed
	clg	%r9,BASED(.Lcleanup_system_call_insn+8)
	jh	0f
	mvc	__LC_SAVE_AREA_SYNC(64),0(%r11)
0:	# check if base register setup + TIF bit load has been done
	clg	%r9,BASED(.Lcleanup_system_call_insn+16)
	jhe	0f
	# set up saved register r12 task struct pointer
	stg	%r12,32(%r11)
	# set up saved register r13 __TASK_thread offset
	mvc	40(8,%r11),BASED(.Lcleanup_system_call_const)
0:	# check if the user time update has been done
	clg	%r9,BASED(.Lcleanup_system_call_insn+24)
	jh	0f
	lg	%r15,__LC_EXIT_TIMER
	slg	%r15,__LC_SYNC_ENTER_TIMER
	alg	%r15,__LC_USER_TIMER
	stg	%r15,__LC_USER_TIMER
0:	# check if the system time update has been done
	clg	%r9,BASED(.Lcleanup_system_call_insn+32)
	jh	0f
	lg	%r15,__LC_LAST_UPDATE_TIMER
	slg	%r15,__LC_EXIT_TIMER
	alg	%r15,__LC_SYSTEM_TIMER
	stg	%r15,__LC_SYSTEM_TIMER
0:	# update accounting time stamp
	mvc	__LC_LAST_UPDATE_TIMER(8),__LC_SYNC_ENTER_TIMER
	# set up saved register r11
	lg	%r15,__LC_KERNEL_STACK
	la	%r9,STACK_FRAME_OVERHEAD(%r15)
	stg	%r9,24(%r11)		# r11 pt_regs pointer
	# fill pt_regs
	mvc	__PT_R8(64,%r9),__LC_SAVE_AREA_SYNC
	stmg	%r0,%r7,__PT_R0(%r9)
	mvc	__PT_PSW(16,%r9),__LC_SVC_OLD_PSW
	mvc	__PT_INT_CODE(4,%r9),__LC_SVC_ILC
	xc	__PT_FLAGS(8,%r9),__PT_FLAGS(%r9)
	mvi	__PT_FLAGS+7(%r9),_PIF_SYSCALL
	# setup saved register r15
	stg	%r15,56(%r11)		# r15 stack pointer
	# set new psw address and exit
	larl	%r9,.Lsysc_do_svc
	br	%r14
.Lcleanup_system_call_insn:
	.quad	system_call
	.quad	.Lsysc_stmg
	.quad	.Lsysc_per
	.quad	.Lsysc_vtime+36
	.quad	.Lsysc_vtime+42
.Lcleanup_system_call_const:
	.quad	__TASK_thread

.Lcleanup_sysc_tif:
	larl	%r9,.Lsysc_tif
	br	%r14

.Lcleanup_sysc_restore:
	# check if stpt has been executed
	clg	%r9,BASED(.Lcleanup_sysc_restore_insn)
	jh	0f
	mvc	__LC_EXIT_TIMER(8),__LC_ASYNC_ENTER_TIMER
	cghi	%r11,__LC_SAVE_AREA_ASYNC
	je	0f
	mvc	__LC_EXIT_TIMER(8),__LC_MCCK_ENTER_TIMER
0:	clg	%r9,BASED(.Lcleanup_sysc_restore_insn+8)
	je	1f
	lg	%r9,24(%r11)		# get saved pointer to pt_regs
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r9)
	mvc	0(64,%r11),__PT_R8(%r9)
	lmg	%r0,%r7,__PT_R0(%r9)
1:	lmg	%r8,%r9,__LC_RETURN_PSW
	br	%r14
.Lcleanup_sysc_restore_insn:
	.quad	.Lsysc_exit_timer
	.quad	.Lsysc_done - 4

.Lcleanup_io_tif:
	larl	%r9,.Lio_tif
	br	%r14

.Lcleanup_io_restore:
	# check if stpt has been executed
	clg	%r9,BASED(.Lcleanup_io_restore_insn)
	jh	0f
	mvc	__LC_EXIT_TIMER(8),__LC_MCCK_ENTER_TIMER
0:	clg	%r9,BASED(.Lcleanup_io_restore_insn+8)
	je	1f
	lg	%r9,24(%r11)		# get saved r11 pointer to pt_regs
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r9)
	mvc	0(64,%r11),__PT_R8(%r9)
	lmg	%r0,%r7,__PT_R0(%r9)
1:	lmg	%r8,%r9,__LC_RETURN_PSW
	br	%r14
.Lcleanup_io_restore_insn:
	.quad	.Lio_exit_timer
	.quad	.Lio_done - 4

.Lcleanup_idle:
	ni	__LC_CPU_FLAGS+7,255-_CIF_ENABLED_WAIT
	# copy interrupt clock & cpu timer
	mvc	__CLOCK_IDLE_EXIT(8,%r2),__LC_INT_CLOCK
	mvc	__TIMER_IDLE_EXIT(8,%r2),__LC_ASYNC_ENTER_TIMER
	cghi	%r11,__LC_SAVE_AREA_ASYNC
	je	0f
	mvc	__CLOCK_IDLE_EXIT(8,%r2),__LC_MCCK_CLOCK
	mvc	__TIMER_IDLE_EXIT(8,%r2),__LC_MCCK_ENTER_TIMER
0:	# check if stck & stpt have been executed
	clg	%r9,BASED(.Lcleanup_idle_insn)
	jhe	1f
	mvc	__CLOCK_IDLE_ENTER(8,%r2),__CLOCK_IDLE_EXIT(%r2)
	mvc	__TIMER_IDLE_ENTER(8,%r2),__TIMER_IDLE_EXIT(%r2)
1:	# calculate idle cycles
#ifdef CONFIG_SMP
	clg	%r9,BASED(.Lcleanup_idle_insn)
	jl	3f
	larl	%r1,smp_cpu_mtid
	llgf	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	3f
	.insn	rsy,0xeb0000000017,%r1,5,__SF_EMPTY+80(%r15)
	larl	%r3,mt_cycles
	ag	%r3,__LC_PERCPU_OFFSET
	la	%r4,__SF_EMPTY+16(%r15)
2:	lg	%r0,0(%r3)
	slg	%r0,0(%r4)
	alg	%r0,64(%r4)
	stg	%r0,0(%r3)
	la	%r3,8(%r3)
	la	%r4,8(%r4)
	brct	%r1,2b
#endif
3:	# account system time going idle
	lg	%r9,__LC_STEAL_TIMER
	alg	%r9,__CLOCK_IDLE_ENTER(%r2)
	slg	%r9,__LC_LAST_UPDATE_CLOCK
	stg	%r9,__LC_STEAL_TIMER
	mvc	__LC_LAST_UPDATE_CLOCK(8),__CLOCK_IDLE_EXIT(%r2)
	lg	%r9,__LC_SYSTEM_TIMER
	alg	%r9,__LC_LAST_UPDATE_TIMER
	slg	%r9,__TIMER_IDLE_ENTER(%r2)
	stg	%r9,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__TIMER_IDLE_EXIT(%r2)
	# prepare return psw
	nihh	%r8,0xfcfd		# clear irq & wait state bits
	lg	%r9,48(%r11)		# return from psw_idle
	br	%r14
.Lcleanup_idle_insn:
	.quad	.Lpsw_idle_lpsw

.Lcleanup_save_fpu_regs:
	larl	%r9,save_fpu_regs
	br	%r14

.Lcleanup_load_fpu_regs:
	larl	%r9,load_fpu_regs
	br	%r14

/*
 * Integer constants
 */
	.align	8
.Lcritical_start:
	.quad	.L__critical_start
.Lcritical_length:
	.quad	.L__critical_end - .L__critical_start
#if IS_ENABLED(CONFIG_KVM)
.Lsie_critical_start:
	.quad	.Lsie_gmap
.Lsie_critical_length:
	.quad	.Lsie_done - .Lsie_gmap
#endif

	.section .rodata, "a"
#define SYSCALL(esame,emu)	.long esame
	.globl	sys_call_table
sys_call_table:
#include "syscalls.S"
#undef SYSCALL

#ifdef CONFIG_COMPAT

#define SYSCALL(esame,emu)	.long emu
	.globl	sys_call_table_emu
sys_call_table_emu:
#include "syscalls.S"
#undef SYSCALL
#endif
