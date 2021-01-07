/* SPDX-License-Identifier: GPL-2.0 */
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
#include <asm/alternative-asm.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/ctl_reg.h>
#include <asm/dwarf.h>
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
#include <asm/nospec-insn.h>

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
_CIF_WORK	= (_CIF_ASCE_PRIMARY | _CIF_ASCE_SECONDARY | _CIF_FPU)
_PIF_WORK	= (_PIF_PER_TRAP | _PIF_SYSCALL_RESTART)

_LPP_OFFSET	= __LC_LPP

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

	.macro	CHECK_STACK savearea
#ifdef CONFIG_CHECK_STACK
	tml	%r15,STACK_SIZE - CONFIG_STACK_GUARD
	lghi	%r14,\savearea
	jz	stack_overflow
#endif
	.endm

	.macro	CHECK_VMAP_STACK savearea,oklabel
#ifdef CONFIG_VMAP_STACK
	lgr	%r14,%r15
	nill	%r14,0x10000 - STACK_SIZE
	oill	%r14,STACK_INIT
	clg	%r14,__LC_KERNEL_STACK
	je	\oklabel
	clg	%r14,__LC_ASYNC_STACK
	je	\oklabel
	clg	%r14,__LC_NODAT_STACK
	je	\oklabel
	clg	%r14,__LC_RESTART_STACK
	je	\oklabel
	lghi	%r14,\savearea
	j	stack_overflow
#else
	j	\oklabel
#endif
	.endm

	.macro	SWITCH_ASYNC savearea,timer
	tmhh	%r8,0x0001		# interrupting from user ?
	jnz	2f
#if IS_ENABLED(CONFIG_KVM)
	lgr	%r14,%r9
	larl	%r13,.Lsie_gmap
	slgr	%r14,%r13
	lghi	%r13,.Lsie_done - .Lsie_gmap
	clgr	%r14,%r13
	jhe	0f
	lghi	%r11,\savearea		# inside critical section, do cleanup
	brasl	%r14,.Lcleanup_sie
#endif
0:	larl	%r13,.Lpsw_idle_exit
	cgr	%r13,%r9
	jne	1f

	mvc	__CLOCK_IDLE_EXIT(8,%r2), __LC_INT_CLOCK
	mvc	__TIMER_IDLE_EXIT(8,%r2), __LC_ASYNC_ENTER_TIMER
	# account system time going idle
	ni	__LC_CPU_FLAGS+7,255-_CIF_ENABLED_WAIT

	lg	%r13,__LC_STEAL_TIMER
	alg	%r13,__CLOCK_IDLE_ENTER(%r2)
	slg	%r13,__LC_LAST_UPDATE_CLOCK
	stg	%r13,__LC_STEAL_TIMER

	mvc	__LC_LAST_UPDATE_CLOCK(8),__CLOCK_IDLE_EXIT(%r2)

	lg	%r13,__LC_SYSTEM_TIMER
	alg	%r13,__LC_LAST_UPDATE_TIMER
	slg	%r13,__TIMER_IDLE_ENTER(%r2)
	stg	%r13,__LC_SYSTEM_TIMER
	mvc	__LC_LAST_UPDATE_TIMER(8),__TIMER_IDLE_EXIT(%r2)

	nihh	%r8,0xfcfd		# clear wait state and irq bits
1:	lg	%r14,__LC_ASYNC_STACK	# are we already on the target stack?
	slgr	%r14,%r15
	srag	%r14,%r14,STACK_SHIFT
	jnz	3f
	CHECK_STACK \savearea
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	j	4f
2:	UPDATE_VTIME %r14,%r15,\timer
	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
3:	lg	%r15,__LC_ASYNC_STACK	# load async stack
4:	la	%r11,STACK_FRAME_OVERHEAD(%r15)
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

	.macro RESTORE_SM_CLEAR_PER
	stg	%r8,__LC_RETURN_PSW
	ni	__LC_RETURN_PSW,0xbf
	ssm	__LC_RETURN_PSW
	.endm

	.macro ENABLE_INTS
	stosm	__SF_EMPTY(%r15),3
	.endm

	.macro ENABLE_INTS_TRACE
	TRACE_IRQS_ON
	ENABLE_INTS
	.endm

	.macro DISABLE_INTS
	stnsm	__SF_EMPTY(%r15),0xfc
	.endm

	.macro DISABLE_INTS_TRACE
	DISABLE_INTS
	TRACE_IRQS_OFF
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

	.macro BPOFF
	ALTERNATIVE "", ".long 0xb2e8c000", 82
	.endm

	.macro BPON
	ALTERNATIVE "", ".long 0xb2e8d000", 82
	.endm

	.macro BPENTER tif_ptr,tif_mask
	ALTERNATIVE "TSTMSK \tif_ptr,\tif_mask; jz .+8; .long 0xb2e8d000", \
		    "", 82
	.endm

	.macro BPEXIT tif_ptr,tif_mask
	TSTMSK	\tif_ptr,\tif_mask
	ALTERNATIVE "jz .+8;  .long 0xb2e8c000", \
		    "jnz .+8; .long 0xb2e8d000", 82
	.endm

	GEN_BR_THUNK %r9
	GEN_BR_THUNK %r14
	GEN_BR_THUNK %r14,%r11

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

ENTRY(__bpon)
	.globl __bpon
	BPON
	BR_EX	%r14
ENDPROC(__bpon)

/*
 * Scheduler resume function, called by switch_to
 *  gpr2 = (task_struct *) prev
 *  gpr3 = (task_struct *) next
 * Returns:
 *  gpr2 = prev
 */
ENTRY(__switch_to)
	stmg	%r6,%r15,__SF_GPRS(%r15)	# store gprs of prev task
	lghi	%r4,__TASK_stack
	lghi	%r1,__TASK_thread
	llill	%r5,STACK_INIT
	stg	%r15,__THREAD_ksp(%r1,%r2)	# store kernel stack of prev
	lg	%r15,0(%r4,%r3)			# start of kernel stack of next
	agr	%r15,%r5			# end of kernel stack of next
	stg	%r3,__LC_CURRENT		# store task struct of next
	stg	%r15,__LC_KERNEL_STACK		# store end of kernel stack
	lg	%r15,__THREAD_ksp(%r1,%r3)	# load kernel stack of next
	aghi	%r3,__TASK_pid
	mvc	__LC_CURRENT_PID(4,%r0),0(%r3)	# store pid of next
	lmg	%r6,%r15,__SF_GPRS(%r15)	# load gprs of next task
	ALTERNATIVE "", ".insn s,0xb2800000,_LPP_OFFSET", 40
	BR_EX	%r14
ENDPROC(__switch_to)

#if IS_ENABLED(CONFIG_KVM)
/*
 * sie64a calling convention:
 * %r2 pointer to sie control block
 * %r3 guest register save area
 */
ENTRY(sie64a)
	stmg	%r6,%r14,__SF_GPRS(%r15)	# save kernel registers
	lg	%r12,__LC_CURRENT
	stg	%r2,__SF_SIE_CONTROL(%r15)	# save control block pointer
	stg	%r3,__SF_SIE_SAVEAREA(%r15)	# save guest register save area
	xc	__SF_SIE_REASON(8,%r15),__SF_SIE_REASON(%r15) # reason code = 0
	mvc	__SF_SIE_FLAGS(8,%r15),__TI_flags(%r12) # copy thread flags
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
	lg	%r14,__SF_SIE_CONTROL(%r15)	# get control block pointer
	oi	__SIE_PROG0C+3(%r14),1		# we are going into SIE now
	tm	__SIE_PROG20+3(%r14),3		# last exit...
	jnz	.Lsie_skip
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lsie_skip			# exit if fp/vx regs changed
	BPEXIT	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
.Lsie_entry:
	sie	0(%r14)
	BPOFF
	BPENTER	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
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
	lg	%r14,__SF_SIE_SAVEAREA(%r15)	# load guest register save area
	stmg	%r0,%r13,0(%r14)		# save guest gprs 0-13
	xgr	%r0,%r0				# clear guest registers to
	xgr	%r1,%r1				# prevent speculative use
	xgr	%r2,%r2
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	lmg	%r6,%r14,__SF_GPRS(%r15)	# restore kernel registers
	lg	%r2,__SF_SIE_REASON(%r15)	# return exit reason code
	BR_EX	%r14
.Lsie_fault:
	lghi	%r14,-EFAULT
	stg	%r14,__SF_SIE_REASON(%r15)	# set exit reason code
	j	sie_exit

	EX_TABLE(.Lrewind_pad6,.Lsie_fault)
	EX_TABLE(.Lrewind_pad4,.Lsie_fault)
	EX_TABLE(.Lrewind_pad2,.Lsie_fault)
	EX_TABLE(sie_exit,.Lsie_fault)
ENDPROC(sie64a)
EXPORT_SYMBOL(sie64a)
EXPORT_SYMBOL(sie_exit)
#endif

/*
 * SVC interrupt handler routine. System calls are synchronous events and
 * are entered with interrupts disabled.
 */

ENTRY(system_call)
	stpt	__LC_SYNC_ENTER_TIMER
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	BPOFF
	lg	%r12,__LC_CURRENT
	lghi	%r14,_PIF_SYSCALL
.Lsysc_per:
	lghi	%r13,__TASK_thread
	lg	%r15,__LC_KERNEL_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)	# pointer to pt_regs
	UPDATE_VTIME %r8,%r9,__LC_SYNC_ENTER_TIMER
	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
	stmg	%r0,%r7,__PT_R0(%r11)
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	mvc	__PT_PSW(16,%r11),__LC_SVC_OLD_PSW
	mvc	__PT_INT_CODE(4,%r11),__LC_SVC_ILC
	stg	%r14,__PT_FLAGS(%r11)
	ENABLE_INTS
.Lsysc_do_svc:
	# clear user controlled register to prevent speculative use
	xgr	%r0,%r0
	# load address of system call table
	lg	%r10,__THREAD_sysc_table(%r13,%r12)
	llgh	%r8,__PT_INT_CODE+2(%r11)
	slag	%r8,%r8,3			# shift and test for svc 0
	jnz	.Lsysc_nr_ok
	# svc 0: system call number in %r1
	llgfr	%r1,%r1				# clear high word in r1
	sth	%r1,__PT_INT_CODE+2(%r11)
	cghi	%r1,NR_syscalls
	jnl	.Lsysc_nr_ok
	slag	%r8,%r1,3
.Lsysc_nr_ok:
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	stg	%r2,__PT_ORIG_GPR2(%r11)
	stg	%r7,STACK_FRAME_OVERHEAD(%r15)
	lg	%r9,0(%r8,%r10)			# get system call add.
	TSTMSK	__TI_flags(%r12),_TIF_TRACE
	jnz	.Lsysc_tracesys
	BASR_EX	%r14,%r9			# call sys_xxxx
	stg	%r2,__PT_R2(%r11)		# store return value

.Lsysc_return:
#ifdef CONFIG_DEBUG_RSEQ
	lgr	%r2,%r11
	brasl	%r14,rseq_syscall
#endif
	LOCKDEP_SYS_EXIT
.Lsysc_tif:
	DISABLE_INTS
	TSTMSK	__PT_FLAGS(%r11),_PIF_WORK
	jnz	.Lsysc_work
	TSTMSK	__TI_flags(%r12),_TIF_WORK
	jnz	.Lsysc_work			# check for work
	TSTMSK	__LC_CPU_FLAGS,(_CIF_WORK-_CIF_FPU)
	jnz	.Lsysc_work
	BPEXIT	__TI_flags(%r12),_TIF_ISOLATE_BP
.Lsysc_restore:
	DISABLE_INTS
	TSTMSK	__LC_CPU_FLAGS, _CIF_FPU
	jz	.Lsysc_skip_fpu
	brasl	%r14,load_fpu_regs
.Lsysc_skip_fpu:
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r11)
	stpt	__LC_EXIT_TIMER
	lmg	%r0,%r15,__PT_R0(%r11)
	b	__LC_RETURN_LPSWE

#
# One of the work bits is on. Find out which one.
#
.Lsysc_work:
	ENABLE_INTS
	TSTMSK	__TI_flags(%r12),_TIF_NEED_RESCHED
	jo	.Lsysc_reschedule
	TSTMSK	__PT_FLAGS(%r11),_PIF_SYSCALL_RESTART
	jo	.Lsysc_syscall_restart
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
	TSTMSK	__PT_FLAGS(%r11),_PIF_SYSCALL_RESTART
	jo	.Lsysc_syscall_restart
	TSTMSK	__TI_flags(%r12),_TIF_SIGPENDING
	jo	.Lsysc_sigpending
	TSTMSK	__TI_flags(%r12),_TIF_NOTIFY_RESUME
	jo	.Lsysc_notify_resume
	TSTMSK	__LC_CPU_FLAGS,(_CIF_ASCE_PRIMARY|_CIF_ASCE_SECONDARY)
	jnz	.Lsysc_asce
	j	.Lsysc_return

#
# _TIF_NEED_RESCHED is set, call schedule
#
.Lsysc_reschedule:
	larl	%r14,.Lsysc_return
	jg	schedule

#
# _CIF_ASCE_PRIMARY and/or _CIF_ASCE_SECONDARY set, load user space asce
#
.Lsysc_asce:
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_SECONDARY
	lctlg	%c7,%c7,__LC_VDSO_ASCE		# load secondary asce
	TSTMSK	__LC_CPU_FLAGS,_CIF_ASCE_PRIMARY
	jz	.Lsysc_return
#ifndef CONFIG_HAVE_MARCH_Z10_FEATURES
	tm	__LC_STFLE_FAC_LIST+3,0x10	# has MVCOS ?
	jnz	.Lsysc_set_fs_fixup
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_PRIMARY
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	j	.Lsysc_return
.Lsysc_set_fs_fixup:
#endif
	larl	%r14,.Lsysc_return
	jg	set_fs_fixup


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
# _PIF_SYSCALL_RESTART is set, repeat the current system call
#
.Lsysc_syscall_restart:
	ni	__PT_FLAGS+7(%r11),255-_PIF_SYSCALL_RESTART
	lmg	%r1,%r7,__PT_R1(%r11)	# load svc arguments
	lg	%r2,__PT_ORIG_GPR2(%r11)
	j	.Lsysc_do_svc

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
	sllg	%r8,%r2,3
	lg	%r9,0(%r8,%r10)
	lmg	%r3,%r7,__PT_R3(%r11)
	stg	%r7,STACK_FRAME_OVERHEAD(%r15)
	lg	%r2,__PT_ORIG_GPR2(%r11)
	BASR_EX	%r14,%r9		# call sys_xxx
	stg	%r2,__PT_R2(%r11)	# store return value
.Lsysc_tracenogo:
	TSTMSK	__TI_flags(%r12),_TIF_TRACE
	jz	.Lsysc_return
	lgr	%r2,%r11		# pass pointer to pt_regs
	larl	%r14,.Lsysc_return
	jg	do_syscall_trace_exit
ENDPROC(system_call)

#
# a new process exits the kernel with ret_from_fork
#
ENTRY(ret_from_fork)
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	lg	%r12,__LC_CURRENT
	brasl	%r14,schedule_tail
	tm	__PT_PSW+1(%r11),0x01	# forking a kernel thread ?
	jne	.Lsysc_tracenogo
	# it's a kernel thread
	lmg	%r9,%r10,__PT_R9(%r11)	# load gprs
	la	%r2,0(%r10)
	BASR_EX	%r14,%r9
	j	.Lsysc_tracenogo
ENDPROC(ret_from_fork)

ENTRY(kernel_thread_starter)
	la	%r2,0(%r10)
	BASR_EX	%r14,%r9
	j	.Lsysc_tracenogo
ENDPROC(kernel_thread_starter)

/*
 * Program check handler routine
 */

ENTRY(pgm_check_handler)
	stpt	__LC_SYNC_ENTER_TIMER
	BPOFF
	stmg	%r8,%r15,__LC_SAVE_AREA_SYNC
	lg	%r10,__LC_LAST_BREAK
	srag	%r11,%r10,12
	jnz	0f
	/* if __LC_LAST_BREAK is < 4096, it contains one of
	 * the lpswe addresses in lowcore. Set it to 1 (initial state)
	 * to prevent leaking that address to userspace.
	 */
	lghi	%r10,1
0:	lg	%r12,__LC_CURRENT
	lghi	%r11,0
	lmg	%r8,%r9,__LC_PGM_OLD_PSW
	tmhh	%r8,0x0001		# test problem state bit
	jnz	3f			# -> fault in user space
#if IS_ENABLED(CONFIG_KVM)
	# cleanup critical section for program checks in sie64a
	lgr	%r14,%r9
	larl	%r13,.Lsie_gmap
	slgr	%r14,%r13
	lghi	%r13,.Lsie_done - .Lsie_gmap
	clgr	%r14,%r13
	jhe	1f
	lg	%r14,__SF_SIE_CONTROL(%r15)	# get control block pointer
	ni	__SIE_PROG0C+3(%r14),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	larl	%r9,sie_exit			# skip forward to sie_exit
	lghi	%r11,_PIF_GUEST_FAULT
#endif
1:	tmhh	%r8,0x4000		# PER bit set in old PSW ?
	jnz	2f			# -> enabled, can't be a double fault
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jnz	.Lpgm_svcper		# -> single stepped svc
2:	CHECK_STACK __LC_SAVE_AREA_SYNC
	aghi	%r15,-(STACK_FRAME_OVERHEAD + __PT_SIZE)
	# CHECK_VMAP_STACK branches to stack_overflow or 5f
	CHECK_VMAP_STACK __LC_SAVE_AREA_SYNC,5f
3:	UPDATE_VTIME %r14,%r15,__LC_SYNC_ENTER_TIMER
	BPENTER __TI_flags(%r12),_TIF_ISOLATE_BP
	lg	%r15,__LC_KERNEL_STACK
	lgr	%r14,%r12
	aghi	%r14,__TASK_thread	# pointer to thread_struct
	lghi	%r13,__LC_PGM_TDB
	tm	__LC_PGM_ILC+2,0x02	# check for transaction abort
	jz	4f
	mvc	__THREAD_trap_tdb(256,%r14),0(%r13)
4:	stg	%r10,__THREAD_last_break(%r14)
5:	lgr	%r13,%r11
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r2,%r2
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	mvc	__PT_R8(64,%r11),__LC_SAVE_AREA_SYNC
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_INT_CODE(4,%r11),__LC_PGM_ILC
	mvc	__PT_INT_PARM_LONG(8,%r11),__LC_TRANS_EXC_CODE
	stg	%r13,__PT_FLAGS(%r11)
	stg	%r10,__PT_ARGS(%r11)
	tm	__LC_PGM_ILC+3,0x80	# check for per exception
	jz	6f
	tmhh	%r8,0x0001		# kernel per event ?
	jz	.Lpgm_kprobe
	oi	__PT_FLAGS+7(%r11),_PIF_PER_TRAP
	mvc	__THREAD_per_address(8,%r14),__LC_PER_ADDRESS
	mvc	__THREAD_per_cause(2,%r14),__LC_PER_CODE
	mvc	__THREAD_per_paid(1,%r14),__LC_PER_ACCESS_ID
6:	RESTORE_SM_CLEAR_PER
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	larl	%r1,pgm_check_table
	llgh	%r10,__PT_INT_CODE+2(%r11)
	nill	%r10,0x007f
	sll	%r10,3
	je	.Lpgm_return
	lg	%r9,0(%r10,%r1)		# load address of handler routine
	lgr	%r2,%r11		# pass pointer to pt_regs
	BASR_EX	%r14,%r9		# branch to interrupt-handler
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
	RESTORE_SM_CLEAR_PER
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_per_trap
	j	.Lpgm_return

#
# single stepped system call
#
.Lpgm_svcper:
	mvc	__LC_RETURN_PSW(8),__LC_SVC_NEW_PSW
	larl	%r14,.Lsysc_per
	stg	%r14,__LC_RETURN_PSW+8
	lghi	%r14,_PIF_SYSCALL | _PIF_PER_TRAP
	lpswe	__LC_RETURN_PSW		# branch to .Lsysc_per
ENDPROC(pgm_check_handler)

/*
 * IO interrupt handler routine
 */
ENTRY(io_int_handler)
	STCK	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	BPOFF
	stmg	%r8,%r15,__LC_SAVE_AREA_ASYNC
	lg	%r12,__LC_CURRENT
	lmg	%r8,%r9,__LC_IO_OLD_PSW
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_ENTER_TIMER
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r2,%r2
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
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
	TSTMSK	__TI_flags(%r12),_TIF_WORK
	jnz	.Lio_work		# there is work to do (signals etc.)
	TSTMSK	__LC_CPU_FLAGS,_CIF_WORK
	jnz	.Lio_work
.Lio_restore:
	TRACE_IRQS_ON
	mvc	__LC_RETURN_PSW(16),__PT_PSW(%r11)
	tm	__PT_PSW+1(%r11),0x01	# returning to user ?
	jno	.Lio_exit_kernel
	BPEXIT	__TI_flags(%r12),_TIF_ISOLATE_BP
	stpt	__LC_EXIT_TIMER
.Lio_exit_kernel:
	lmg	%r0,%r15,__PT_R0(%r11)
	b	__LC_RETURN_LPSWE
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
#ifdef CONFIG_PREEMPTION
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
	j	.Lio_return

#
# _CIF_ASCE_PRIMARY and/or CIF_ASCE_SECONDARY set, load user space asce
#
.Lio_asce:
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_SECONDARY
	lctlg	%c7,%c7,__LC_VDSO_ASCE		# load secondary asce
	TSTMSK	__LC_CPU_FLAGS,_CIF_ASCE_PRIMARY
	jz	.Lio_return
#ifndef CONFIG_HAVE_MARCH_Z10_FEATURES
	tm	__LC_STFLE_FAC_LIST+3,0x10	# has MVCOS ?
	jnz	.Lio_set_fs_fixup
	ni	__LC_CPU_FLAGS+7,255-_CIF_ASCE_PRIMARY
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	j	.Lio_return
.Lio_set_fs_fixup:
#endif
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
	ENABLE_INTS_TRACE
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,gs_load_bc_cb
	DISABLE_INTS_TRACE
	j	.Lio_return

#
# _TIF_NEED_RESCHED is set, call schedule
#
.Lio_reschedule:
	ENABLE_INTS_TRACE
	brasl	%r14,schedule		# call scheduler
	DISABLE_INTS_TRACE
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
	ENABLE_INTS_TRACE
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_signal
	DISABLE_INTS_TRACE
	j	.Lio_return

#
# _TIF_NOTIFY_RESUME or is set, call do_notify_resume
#
.Lio_notify_resume:
	ENABLE_INTS_TRACE
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,do_notify_resume
	DISABLE_INTS_TRACE
	j	.Lio_return
ENDPROC(io_int_handler)

/*
 * External interrupt handler routine
 */
ENTRY(ext_int_handler)
	STCK	__LC_INT_CLOCK
	stpt	__LC_ASYNC_ENTER_TIMER
	BPOFF
	stmg	%r8,%r15,__LC_SAVE_AREA_ASYNC
	lg	%r12,__LC_CURRENT
	lmg	%r8,%r9,__LC_EXT_OLD_PSW
	SWITCH_ASYNC __LC_SAVE_AREA_ASYNC,__LC_ASYNC_ENTER_TIMER
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r2,%r2
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
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
ENDPROC(ext_int_handler)

/*
 * Load idle PSW.
 */
ENTRY(psw_idle)
	stg	%r3,__SF_EMPTY(%r15)
	larl	%r1,.Lpsw_idle_exit
	stg	%r1,__SF_EMPTY+8(%r15)
	larl	%r1,smp_cpu_mtid
	llgf	%r1,0(%r1)
	ltgr	%r1,%r1
	jz	.Lpsw_idle_stcctm
	.insn	rsy,0xeb0000000017,%r1,5,__SF_EMPTY+16(%r15)
.Lpsw_idle_stcctm:
	oi	__LC_CPU_FLAGS+7,_CIF_ENABLED_WAIT
	BPON
	STCK	__CLOCK_IDLE_ENTER(%r2)
	stpt	__TIMER_IDLE_ENTER(%r2)
	lpswe	__SF_EMPTY(%r15)
.Lpsw_idle_exit:
	BR_EX	%r14
ENDPROC(psw_idle)

/*
 * Store floating-point controls and floating-point or vector register
 * depending whether the vector facility is available.	A critical section
 * cleanup assures that the registers are stored even if interrupted for
 * some other work.  The CIF_FPU flag is set to trigger a lazy restore
 * of the register contents at return from io or a system call.
 */
ENTRY(save_fpu_regs)
	stnsm	__SF_EMPTY(%r15),0xfc
	lg	%r2,__LC_CURRENT
	aghi	%r2,__TASK_thread
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jo	.Lsave_fpu_regs_exit
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
.Lsave_fpu_regs_exit:
	ssm	__SF_EMPTY(%r15)
	BR_EX	%r14
.Lsave_fpu_regs_end:
ENDPROC(save_fpu_regs)
EXPORT_SYMBOL(save_fpu_regs)

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
	stnsm	__SF_EMPTY(%r15),0xfc
	lg	%r4,__LC_CURRENT
	aghi	%r4,__TASK_thread
	TSTMSK	__LC_CPU_FLAGS,_CIF_FPU
	jno	.Lload_fpu_regs_exit
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
.Lload_fpu_regs_exit:
	ssm	__SF_EMPTY(%r15)
	BR_EX	%r14
.Lload_fpu_regs_end:
ENDPROC(load_fpu_regs)

/*
 * Machine check handler routines
 */
ENTRY(mcck_int_handler)
	STCK	__LC_MCCK_CLOCK
	BPOFF
	la	%r1,4095		# validate r1
	spt	__LC_CPU_TIMER_SAVE_AREA-4095(%r1)	# validate cpu timer
	sckc	__LC_CLOCK_COMPARATOR			# validate comparator
	lam	%a0,%a15,__LC_AREGS_SAVE_AREA-4095(%r1) # validate acrs
	lmg	%r0,%r15,__LC_GPREGS_SAVE_AREA-4095(%r1)# validate gprs
	lg	%r12,__LC_CURRENT
	lmg	%r8,%r9,__LC_MCK_OLD_PSW
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_SYSTEM_DAMAGE
	jo	.Lmcck_panic		# yes -> rest of mcck code invalid
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_CR_VALID
	jno	.Lmcck_panic		# control registers invalid -> panic
	la	%r14,4095
	lctlg	%c0,%c15,__LC_CREGS_SAVE_AREA-4095(%r14) # validate ctl regs
	ptlb
	lg	%r11,__LC_MCESAD-4095(%r14) # extended machine check save area
	nill	%r11,0xfc00		# MCESA_ORIGIN_MASK
	TSTMSK	__LC_CREGS_SAVE_AREA+16-4095(%r14),CR2_GUARDED_STORAGE
	jno	0f
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_GS_VALID
	jno	0f
	.insn	 rxy,0xe3000000004d,0,__MCESA_GS_SAVE_AREA(%r11) # LGSC
0:	l	%r14,__LC_FP_CREG_SAVE_AREA-4095(%r14)
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_FC_VALID
	jo	0f
	sr	%r14,%r14
0:	sfpc	%r14
	TSTMSK	__LC_MACHINE_FLAGS,MACHINE_FLAG_VX
	jo	0f
	lghi	%r14,__LC_FPREGS_SAVE_AREA
	ld	%f0,0(%r14)
	ld	%f1,8(%r14)
	ld	%f2,16(%r14)
	ld	%f3,24(%r14)
	ld	%f4,32(%r14)
	ld	%f5,40(%r14)
	ld	%f6,48(%r14)
	ld	%f7,56(%r14)
	ld	%f8,64(%r14)
	ld	%f9,72(%r14)
	ld	%f10,80(%r14)
	ld	%f11,88(%r14)
	ld	%f12,96(%r14)
	ld	%f13,104(%r14)
	ld	%f14,112(%r14)
	ld	%f15,120(%r14)
	j	1f
0:	VLM	%v0,%v15,0,%r11
	VLM	%v16,%v31,256,%r11
1:	lghi	%r14,__LC_CPU_TIMER_SAVE_AREA
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
3:	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_PSW_MWP_VALID
	jno	.Lmcck_panic
	tmhh	%r8,0x0001		# interrupting from user ?
	jnz	4f
	TSTMSK	__LC_MCCK_CODE,MCCK_CODE_PSW_IA_VALID
	jno	.Lmcck_panic
4:	ssm	__LC_PGM_NEW_PSW	# turn dat on, keep irqs off
	SWITCH_ASYNC __LC_GPREGS_SAVE_AREA+64,__LC_MCCK_ENTER_TIMER
.Lmcck_skip:
	lghi	%r14,__LC_GPREGS_SAVE_AREA+64
	stmg	%r0,%r7,__PT_R0(%r11)
	# clear user controlled registers to prevent speculative use
	xgr	%r0,%r0
	xgr	%r1,%r1
	xgr	%r2,%r2
	xgr	%r3,%r3
	xgr	%r4,%r4
	xgr	%r5,%r5
	xgr	%r6,%r6
	xgr	%r7,%r7
	xgr	%r10,%r10
	mvc	__PT_R8(64,%r11),0(%r14)
	stmg	%r8,%r9,__PT_PSW(%r11)
	xc	__PT_FLAGS(8,%r11),__PT_FLAGS(%r11)
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	brasl	%r14,s390_do_machine_check
	cghi	%r2,0
	je	.Lmcck_return
	lg	%r1,__LC_KERNEL_STACK	# switch to kernel stack
	mvc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r1),0(%r11)
	xc	__SF_BACKCHAIN(8,%r1),__SF_BACKCHAIN(%r1)
	la	%r11,STACK_FRAME_OVERHEAD(%r1)
	lgr	%r15,%r1
	TRACE_IRQS_OFF
	brasl	%r14,s390_handle_mcck
	TRACE_IRQS_ON
.Lmcck_return:
	lmg	%r0,%r10,__PT_R0(%r11)
	mvc	__LC_RETURN_MCCK_PSW(16),__PT_PSW(%r11) # move return PSW
	tm	__LC_RETURN_MCCK_PSW+1,0x01 # returning to user ?
	jno	0f
	BPEXIT	__TI_flags(%r12),_TIF_ISOLATE_BP
	stpt	__LC_EXIT_TIMER
0:	lmg	%r11,%r15,__PT_R11(%r11)
	b	__LC_RETURN_MCCK_LPSWE

.Lmcck_panic:
	lg	%r15,__LC_NODAT_STACK
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	j	.Lmcck_skip
ENDPROC(mcck_int_handler)

#
# PSW restart interrupt handler
#
ENTRY(restart_int_handler)
	ALTERNATIVE "", ".insn s,0xb2800000,_LPP_OFFSET", 40
	stg	%r15,__LC_SAVE_AREA_RESTART
	lg	%r15,__LC_RESTART_STACK
	xc	STACK_FRAME_OVERHEAD(__PT_SIZE,%r15),STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r14,STACK_FRAME_OVERHEAD+__PT_R0(%r15)
	mvc	STACK_FRAME_OVERHEAD+__PT_R15(8,%r15),__LC_SAVE_AREA_RESTART
	mvc	STACK_FRAME_OVERHEAD+__PT_PSW(16,%r15),__LC_RST_OLD_PSW
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
ENDPROC(restart_int_handler)

	.section .kprobes.text, "ax"

#if defined(CONFIG_CHECK_STACK) || defined(CONFIG_VMAP_STACK)
/*
 * The synchronous or the asynchronous stack overflowed. We are dead.
 * No need to properly save the registers, we are going to panic anyway.
 * Setup a pt_regs so that show_trace can provide a good call trace.
 */
ENTRY(stack_overflow)
	lg	%r15,__LC_NODAT_STACK	# change to panic stack
	la	%r11,STACK_FRAME_OVERHEAD(%r15)
	stmg	%r0,%r7,__PT_R0(%r11)
	stmg	%r8,%r9,__PT_PSW(%r11)
	mvc	__PT_R8(64,%r11),0(%r14)
	stg	%r10,__PT_ORIG_GPR2(%r11) # store last break to orig_gpr2
	xc	__SF_BACKCHAIN(8,%r15),__SF_BACKCHAIN(%r15)
	lgr	%r2,%r11		# pass pointer to pt_regs
	jg	kernel_stack_overflow
ENDPROC(stack_overflow)
#endif

#if IS_ENABLED(CONFIG_KVM)
.Lcleanup_sie:
	cghi	%r11,__LC_SAVE_AREA_ASYNC	#Is this in normal interrupt?
	je	1f
	larl	%r13,.Lsie_entry
	slgr	%r9,%r13
	larl	%r13,.Lsie_skip
	clgr	%r9,%r13
	jh	1f
	oi	__LC_CPU_FLAGS+7, _CIF_MCCK_GUEST
1:	BPENTER	__SF_SIE_FLAGS(%r15),(_TIF_ISOLATE_BP|_TIF_ISOLATE_BP_GUEST)
	lg	%r9,__SF_SIE_CONTROL(%r15)	# get control block pointer
	ni	__SIE_PROG0C+3(%r9),0xfe	# no longer in SIE
	lctlg	%c1,%c1,__LC_USER_ASCE		# load primary asce
	larl	%r9,sie_exit			# skip forward to sie_exit
	BR_EX	%r14,%r11

#endif
	.section .rodata, "a"
#define SYSCALL(esame,emu)	.quad __s390x_ ## esame
	.globl	sys_call_table
sys_call_table:
#include "asm/syscall_table.h"
#undef SYSCALL

#ifdef CONFIG_COMPAT

#define SYSCALL(esame,emu)	.quad __s390_ ## emu
	.globl	sys_call_table_emu
sys_call_table_emu:
#include "asm/syscall_table.h"
#undef SYSCALL
#endif
