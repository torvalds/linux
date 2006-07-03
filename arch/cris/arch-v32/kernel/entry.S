/*
 *  Copyright (C) 2000-2003 Axis Communications AB
 *
 *  Authors:	Bjorn Wesen (bjornw@axis.com)
 *              Tobias Anderberg (tobiasa@axis.com), CRISv32 port.
 *
 * Code for the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_system_call':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

#include <asm/arch/hwregs/asm/reg_map_asm.h>
#include <asm/arch/hwregs/asm/intr_vect_defs_asm.h>

	;; Exported functions.
	.globl system_call
	.globl ret_from_intr
	.globl ret_from_fork
	.globl resume
	.globl multiple_interrupt
	.globl nmi_interrupt
	.globl spurious_interrupt
	.globl do_sigtrap
	.globl gdb_handle_exception
	.globl sys_call_table

	; Check if preemptive kernel scheduling should be done.
#ifdef CONFIG_PREEMPT
_resume_kernel:
	di
	; Load current task struct.
	movs.w	-8192, $r0		; THREAD_SIZE = 8192
	and.d	$sp, $r0

	addoq	+TI_preempt_count, $r0, $acr
	move.d	[$acr], $r10		; Preemption disabled?
	bne	_Rexit
	nop

_need_resched:
	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r10
	btstq	TIF_NEED_RESCHED, $r10	; Check if need_resched is set.
	bpl	_Rexit
	nop

	; Do preemptive kernel scheduling.
	jsr	preempt_schedule_irq
	nop

	; Load new task struct.
	movs.w	-8192, $r0		; THREAD_SIZE = 8192.
	and.d	$sp, $r0

	; One more time with new task.
	ba	_need_resched
	nop
#else
#define _resume_kernel _Rexit
#endif

	; Called at exit from fork. schedule_tail must be called to drop
	; spinlock if CONFIG_PREEMPT.
ret_from_fork:
	jsr schedule_tail
	nop
	ba  ret_from_sys_call
	nop

ret_from_intr:
	;; Check for resched if preemptive kernel, or if we're going back to
	;; user-mode. This test matches the user_regs(regs) macro. Don't simply
	;; test CCS since that doesn't necessarily reflect what mode we'll
	;; return into.
	addoq	+PT_ccs, $sp, $acr
	move.d	[$acr], $r0
	btstq	16, $r0			; User-mode flag.
	bpl	_resume_kernel

	; Note that di below is in delay slot.

_resume_userspace:
	di			; So need_resched and sigpending don't change.

	movs.w	-8192, $r0		; THREAD_SIZE == 8192
	and.d	$sp, $r0

	addoq	+TI_flags, $r0, $acr	; current->work
	move.d	[$acr], $r10
	and.d	_TIF_WORK_MASK, $r10	; Work to be done on return?
	bne	_work_pending
	nop
	ba	_Rexit
	nop

	;; The system_call is called by a BREAK instruction, which looks pretty
	;; much like any other exception.
	;;
	;; System calls can't be made from interrupts but we still stack ERP
	;; to have a complete stack frame.
	;;
	;; In r9 we have the wanted syscall number. Arguments come in r10,r11,r12,
	;; r13,mof,srp
	;;
	;; This function looks on the _surface_ like spaghetti programming, but it's
	;; really designed so that the fast-path does not force cache-loading of
	;; non-used instructions. Only the non-common cases cause the outlined code
	;; to run..

system_call:
	;; Stack-frame similar to the irq heads, which is reversed in
	;; ret_from_sys_call.
	subq	12, $sp		; Skip EXS, EDA.
	move	$erp, [$sp]
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$ccs, [$sp]
	subq	4, $sp
	ei			; Allow IRQs while handling system call
	move	$spc, [$sp]
	subq	4, $sp
	move	$mof, [$sp]
	subq	4, $sp
	move	$srs, [$sp]
	subq	4, $sp
	move.d	$acr, [$sp]
	subq	14*4, $sp	; Make room for R0-R13.
	movem	$r13, [$sp]	; Push R0-R13
	subq	4, $sp
	move.d	$r10, [$sp]	; Push orig_r10.

; Set S-bit when kernel debugging to keep hardware breakpoints active.
#ifdef CONFIG_ETRAX_KGDB
	move $ccs, $r0
	or.d (1<<9), $r0
	move $r0, $ccs
#endif

	movs.w	-ENOSYS, $r0
	addoq	+PT_r10, $sp, $acr
	move.d	$r0, [$acr]

	;; Check if this process is syscall-traced.
	movs.w	-8192, $r0	; THREAD_SIZE == 8192
	and.d	$sp, $r0

	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r0
	btstq	TIF_SYSCALL_TRACE, $r0
	bmi	_syscall_trace_entry
	nop

_syscall_traced:
	;; Check for sanity in the requested syscall number.
	cmpu.w	NR_syscalls, $r9
	bhs	ret_from_sys_call
	lslq	2, $r9		;  Multiply by 4, in the delay slot.

	;; The location on the stack for the register structure is passed as a
	;; seventh argument. Some system calls need this.
	move.d  $sp, $r0
	subq	4, $sp
	move.d	$r0, [$sp]

	;; The registers carrying parameters (R10-R13) are intact. The optional
	;; fifth and sixth parameters is in MOF and SRP respectivly. Put them
	;; back on the stack.
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$mof, [$sp]

	;; Actually to the system call.
	addo.d	+sys_call_table, $r9, $acr
	move.d	[$acr], $acr
	jsr	$acr
	nop

	addq	3*4, $sp		; Pop the mof, srp and regs parameters.
	addoq	+PT_r10, $sp, $acr
	move.d	$r10, [$acr]		; Save the return value.

	moveq	1, $r9			; "Parameter" to ret_from_sys_call to
					; show it was a sys call.

	;; Fall through into ret_from_sys_call to return.

ret_from_sys_call:
	;; R9 is a parameter:
	;;  >= 1 from syscall
	;;     0 from irq

	;; Get the current task-struct pointer.
	movs.w	-8192, $r0	; THREAD_SIZE == 8192
	and.d	$sp, $r0

	di		; Make sure need_resched and sigpending don't change.

	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r1
	and.d	_TIF_ALLWORK_MASK, $r1
	bne	_syscall_exit_work
	nop

_Rexit:
	;; This epilogue MUST match the prologues in multiple_interrupt, irq.h
	;; and ptregs.h.
	addq	4, $sp		; Skip orig_r10.
	movem	[$sp+], $r13	; Registers R0-R13.
	move.d	[$sp+], $acr
	move	[$sp], $srs
	addq	4, $sp
	move	[$sp+], $mof
	move	[$sp+], $spc
	move	[$sp+], $ccs
	move	[$sp+], $srp
	move	[$sp+], $erp
	addq    8, $sp		; Skip EXS, EDA.
	jump	$erp
	rfe			; Restore condition code stack in delay-slot.

	;; We get here after doing a syscall if extra work might need to be done
	;; perform syscall exit tracing if needed.

_syscall_exit_work:
	;; R0 contains current at this point and irq's are disabled.

	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r1
	btstq	TIF_SYSCALL_TRACE, $r1
	bpl	_work_pending
	nop
	ei
	move.d	$r9, $r1		; Preserve R9.
	jsr	do_syscall_trace
	nop
	move.d	$r1, $r9
	ba	_resume_userspace
	nop

_work_pending:
	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r10
	btstq	TIF_NEED_RESCHED, $r10	; Need resched?
	bpl	_work_notifysig		; No, must be signal/notify.
	nop

_work_resched:
	move.d	$r9, $r1		; Preserve R9.
	jsr	schedule
	nop
	move.d	$r1, $r9
	di

	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r1
	and.d	_TIF_WORK_MASK, $r1	; Ignore sycall trace counter.
	beq	_Rexit
	nop
	btstq	TIF_NEED_RESCHED, $r1
	bmi	_work_resched		; current->work.need_resched.
	nop

_work_notifysig:
	;; Deal with pending signals and notify-resume requests.

	addoq	+TI_flags, $r0, $acr
	move.d	[$acr], $r13		; The thread_info_flags parameter.
	move.d	$r9, $r10		; do_notify_resume syscall/irq param.
	moveq	0, $r11			; oldset param - 0 in this case.
	move.d	$sp, $r12		; The regs param.
	jsr	do_notify_resume
	nop

	ba _Rexit
	nop

	;; We get here as a sidetrack when we've entered a syscall with the
	;; trace-bit set. We need to call do_syscall_trace and then continue
	;; with the call.

_syscall_trace_entry:
	;; PT_r10 in the frame contains -ENOSYS as required, at this point.

	jsr	do_syscall_trace
	nop

	;; Now re-enter the syscall code to do the syscall itself. We need to
	;; restore R9 here to contain the wanted syscall, and the other
	;; parameter-bearing registers.
	addoq	+PT_r9, $sp, $acr
	move.d	[$acr], $r9
	addoq	+PT_orig_r10, $sp, $acr
	move.d	[$acr], $r10		; PT_r10 is already -ENOSYS.
	addoq	+PT_r11, $sp, $acr
	move.d	[$acr], $r11
	addoq	+PT_r12, $sp, $acr
	move.d	[$acr], $r12
	addoq	+PT_r13, $sp, $acr
	move.d	[$acr], $r13
	addoq	+PT_mof, $sp, $acr
	move	[$acr], $mof
	addoq	+PT_srp, $sp, $acr
	move	[$acr], $srp

	ba	_syscall_traced
	nop

	;; Resume performs the actual task-switching, by switching stack
	;; pointers. Input arguments are:
	;;
	;; R10 = prev
	;; R11 = next
	;; R12 = thread offset in task struct.
	;;
	;; Returns old current in R10.

resume:
	subq	4, $sp
	move	$srp, [$sp]		; Keep old/new PC on the stack.
	add.d	$r12, $r10		; R10 = current tasks tss.
	addoq	+THREAD_ccs, $r10, $acr
	move	$ccs, [$acr]		; Save IRQ enable state.
	di

	addoq	+THREAD_usp, $r10, $acr
	move	$usp, [$acr]		; Save user-mode stackpointer.

	;; See copy_thread for the reason why register R9 is saved.
	subq	10*4, $sp
	movem	$r9, [$sp]		; Save non-scratch registers and R9.

	addoq	+THREAD_ksp, $r10, $acr
	move.d	$sp, [$acr]		; Save kernel SP for old task.

	move.d	$sp, $r10		; Return last running task in R10.
	and.d   -8192, $r10		; Get thread_info from stackpointer.
	addoq	+TI_task, $r10, $acr
	move.d	[$acr], $r10		; Get task.
	add.d	$r12, $r11		; Find the new tasks tss.
	addoq	+THREAD_ksp, $r11, $acr
	move.d	[$acr], $sp		; Switch to new stackframe.
	movem	[$sp+], $r9		; Restore non-scratch registers and R9.

	addoq	+THREAD_usp, $r11, $acr
	move	[$acr], $usp		; Restore user-mode stackpointer.

	addoq	+THREAD_ccs, $r11, $acr
	move	[$acr], $ccs		; Restore IRQ enable status.
	move.d	[$sp+], $acr
	jump	$acr			; Restore PC.
	nop

nmi_interrupt:

;; If we receive a watchdog interrupt while it is not expected, then set
;; up a canonical frame and dump register contents before dying.

	;; This prologue MUST match the one in irq.h and the struct in ptregs.h!
	subq	12, $sp		;  Skip EXS, EDA.
	move	$nrp, [$sp]
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$ccs, [$sp]
	subq	4, $sp
	move	$spc, [$sp]
	subq	4, $sp
	move	$mof, [$sp]
	subq	4, $sp
	move	$srs, [$sp]
	subq	4, $sp
	move.d	$acr, [$sp]
	subq	14*4, $sp		; Make room for R0-R13.
	movem	$r13, [$sp]		; Push R0-R13.
	subq	4, $sp
	move.d	$r10, [$sp]		; Push orig_r10.
	move.d  REG_ADDR(intr_vect, regi_irq, r_nmi), $r0
	move.d  [$r0], $r0
	btstq	REG_BIT(intr_vect, r_nmi, watchdog), $r0
	bpl     1f
	nop
	jsr 	handle_watchdog_bite	; In time.c.
        move.d	$sp, $r10		; Pointer to registers
1:	btstq	REG_BIT(intr_vect, r_nmi, ext), $r0
	bpl     1f
	nop
	jsr	handle_nmi
	move.d	$sp, $r10		; Pointer to registers
1:	addq    4, $sp			; Skip orig_r10
	movem   [$sp+], $r13
	move.d  [$sp+], $acr
	move    [$sp], $srs
	addq    4, $sp
	move    [$sp+], $mof
	move    [$sp+], $spc
	move    [$sp+], $ccs
	move	[$sp+], $srp
	move	[$sp+], $nrp
	addq    8, $sp			; Skip EXS, EDA.
	jump    $nrp
	rfn

	.comm	cause_of_death, 4	;; Don't declare this anywhere.

spurious_interrupt:
	di
	jump hard_reset_now
	nop

	;; This handles the case when multiple interrupts arrive at the same
	;; time. Jump to the first set interrupt bit in a priotiry fashion. The
	;; hardware will call the unserved interrupts after the handler
	;; finishes.
multiple_interrupt:
	;; This prologue MUST match the one in irq.h and the struct in ptregs.h!
	subq	12, $sp		; Skip EXS, EDA.
	move	$erp, [$sp]
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$ccs, [$sp]
	subq	4, $sp
	move	$spc, [$sp]
	subq	4, $sp
	move	$mof, [$sp]
	subq	4, $sp
	move	$srs, [$sp]
	subq	4, $sp
	move.d	$acr, [$sp]
	subq	14*4, $sp	; Make room for R0-R13.
	movem	$r13, [$sp]	; Push R0-R13.
	subq	4, $sp
	move.d	$r10, [$sp]	; Push orig_r10.

; Set S-bit when kernel debugging to keep hardware breakpoints active.
#ifdef CONFIG_ETRAX_KGDB
	move $ccs, $r0
	or.d (1<<9), $r0
	move $r0, $ccs
#endif

	jsr	crisv32_do_multiple
	move.d	$sp, $r10
	jump    ret_from_intr
	nop

do_sigtrap:
	;; Sigtraps the process that executed the BREAK instruction. Creates a
	;; frame that Rexit expects.
	subq	4, $sp
	move    $eda, [$sp]
	subq    4, $sp
	move    $exs, [$sp]
	subq    4, $sp
	move	$erp, [$sp]
	subq	4, $sp
	move	$srp, [$sp]
	subq	4, $sp
	move	$ccs, [$sp]
	subq	4, $sp
	move	$spc, [$sp]
	subq	4, $sp
	move	$mof, [$sp]
	subq	4, $sp
	move	$srs, [$sp]
	subq	4, $sp
	move.d	$acr, [$sp]
	di				; Need to disable irq's at this point.
	subq	14*4, $sp		; Make room for r0-r13.
	movem	$r13, [$sp]		; Push the r0-r13 registers.
	subq	4, $sp
	move.d	$r10, [$sp]		; Push orig_r10.

	movs.w	-8192, $r9		; THREAD_SIZE == 8192
	and.d	$sp, $r9

	;; thread_info as first parameter
	move.d  $r9, $r10
	moveq	5, $r11			; SIGTRAP as second argument.
	jsr	ugdb_trap_user
	nop
	jump	ret_from_intr		; Use the return routine for interrupts.
	nop

gdb_handle_exception:
	subq	4, $sp
	move.d	$r0, [$sp]
#ifdef CONFIG_ETRAX_KGDB
	move	$ccs, $r0		; U-flag not affected by previous insns.
	btstq	16, $r0			; Test the U-flag.
	bmi	_ugdb_handle_exception	; Go to user mode debugging.
	nop				; Empty delay-slot (cannot pop R0 here).
	ba	kgdb_handle_exception	; Go to kernel debugging.
	move.d	[$sp+], $r0		; Restore R0 in delay slot.
#endif

_ugdb_handle_exception:
	ba	do_sigtrap		; SIGTRAP the offending process.
	move.d	[$sp+], $r0		; Restore R0 in delay slot.

	.data

	.section .rodata,"a"
sys_call_table:
	.long sys_restart_syscall	; 0 - old "setup()" system call, used
					; for restarting.
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_lchown16
	.long sys_ni_syscall	/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid16
	.long sys_getuid16
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall	/* old stty syscall holder */
	.long sys_ni_syscall	/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35  old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall	/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid16
	.long sys_getgid16
	.long sys_signal
	.long sys_geteuid16
	.long sys_getegid16	/* 50 */
	.long sys_acct
	.long sys_umount	/* recycled never used phys( */
	.long sys_ni_syscall	/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall	/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall	/* old ulimit syscall holder */
	.long sys_ni_syscall	/* old sys_olduname holder */
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid16	/* 70 */
	.long sys_setregid16
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_old_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups16	/* 80 */
	.long sys_setgroups16
	.long sys_select	/* was old_select in Linux/E100 */
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir
	.long old_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown16	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall	/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ni_syscall	/* sys_ioperm in i386 */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall	/* old sys_uname holder */
	.long sys_ni_syscall	/* sys_iopl in i386 */
	.long sys_vhangup
	.long sys_ni_syscall	/* old "idle" system call */
	.long sys_ni_syscall	/* vm86old in i386 */
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall	/* sys_modify_ldt */
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_ni_syscall	/* old "create_module" */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall	/* 130:	old "get_kernel_syms" */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* for afs_syscall */
	.long sys_setfsuid16
	.long sys_setfsgid16
	.long sys_llseek	/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min	/* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid16
	.long sys_getresuid16	/* 165 */
	.long sys_ni_syscall	/* sys_vm86 */
	.long sys_ni_syscall	/* Old sys_query_module */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid16	/* 170 */
	.long sys_getresgid16
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64	/* 180 */
	.long sys_pwrite64
	.long sys_chown16
	.long sys_getcwd
	.long sys_capget
	.long sys_capset	/* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall	/* streams1 */
	.long sys_ni_syscall	/* streams2 */
	.long sys_vfork		/* 190 */
	.long sys_getrlimit
	.long sys_mmap2
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64	/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_lchown
	.long sys_getuid
	.long sys_getgid	/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups	/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid	/* 210 */
	.long sys_getresgid
	.long sys_chown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid	/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_mincore
	.long sys_madvise
	.long sys_getdents64	/* 220 */
	.long sys_fcntl64
	.long sys_ni_syscall	/* reserved for TUX */
	.long sys_ni_syscall
	.long sys_gettid
	.long sys_readahead	/* 225 */
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr
	.long sys_getxattr
	.long sys_lgetxattr	/* 230 */
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr	/* 235 */
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_tkill
	.long sys_sendfile64
	.long sys_futex		/* 240 */
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_ni_syscall	/* sys_set_thread_area */
	.long sys_ni_syscall	/* sys_get_thread_area */
	.long sys_io_setup	/* 245 */
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit
	.long sys_io_cancel
	.long sys_fadvise64	/* 250 */
	.long sys_ni_syscall
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl	/* 255 */
	.long sys_epoll_wait
	.long sys_remap_file_pages
 	.long sys_set_tid_address
 	.long sys_timer_create
 	.long sys_timer_settime		/* 260 */
 	.long sys_timer_gettime
 	.long sys_timer_getoverrun
 	.long sys_timer_delete
 	.long sys_clock_settime
 	.long sys_clock_gettime		/* 265 */
 	.long sys_clock_getres
 	.long sys_clock_nanosleep
	.long sys_statfs64
	.long sys_fstatfs64
	.long sys_tgkill	/* 270 */
	.long sys_utimes
 	.long sys_fadvise64_64
	.long sys_ni_syscall	/* sys_vserver */
	.long sys_ni_syscall	/* sys_mbind */
	.long sys_ni_syscall	/* 275 sys_get_mempolicy */
	.long sys_ni_syscall	/* sys_set_mempolicy */
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive	/* 280 */
	.long sys_mq_notify
	.long sys_mq_getsetattr
	.long sys_ni_syscall		/* reserved for kexec */
	.long sys_waitid

        /*
         * NOTE!! This doesn't have to be exact - we just have
         * to make sure we have _enough_ of the "sys_ni_syscall"
         * entries. Don't panic if you notice that this hasn't
         * been shrunk every time we add a new system call.
         */

	.rept NR_syscalls - (.-sys_call_table) / 4
		.long sys_ni_syscall
	.endr

