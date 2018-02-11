/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  linux/arch/cris/entry.S
 *
 *  Copyright (C) 2000, 2001, 2002 Axis Communications AB
 *
 *  Authors:	Bjorn Wesen (bjornw@axis.com)
 */

/*
 * entry.S contains the system-call and fault low-level handling routines.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_system_call':
 *	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in fork.c:copy_process, signal.c:do_signal,
 *	ptrace.c and ptrace.h
 *
 */

#include <linux/linkage.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <arch/sv_addr_ag.h>
#include <asm/errno.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/page.h>
#include <asm/pgtable.h>

	;; functions exported from this file

	.globl system_call
	.globl ret_from_intr
	.globl ret_from_fork
	.globl ret_from_kernel_thread
	.globl resume
	.globl multiple_interrupt
	.globl hwbreakpoint
	.globl IRQ1_interrupt
	.globl spurious_interrupt
	.globl hw_bp_trigs
	.globl mmu_bus_fault
	.globl do_sigtrap
	.globl gdb_handle_breakpoint
	.globl sys_call_table

	;; below are various parts of system_call which are not in the fast-path

#ifdef CONFIG_PREEMPT
	; Check if preemptive kernel scheduling should be done
_resume_kernel:
	di
	; Load current task struct
	movs.w	-8192, $r0	;  THREAD_SIZE = 8192
	and.d	$sp, $r0
	move.d	[$r0+TI_preempt_count], $r10	;  Preemption disabled?
	bne	_Rexit
	nop
_need_resched:
	move.d	[$r0+TI_flags], $r10
	btstq	TIF_NEED_RESCHED, $r10	; Check if need_resched is set
	bpl	_Rexit
	nop
	; Ok, lets's do some preemptive kernel scheduling
	jsr	preempt_schedule_irq
	; Load new task struct
	movs.w	-8192, $r0	;  THREAD_SIZE = 8192
	and.d	$sp, $r0
	; One more time (with new task)
	ba	_need_resched
	nop
#else
#define _resume_kernel _Rexit
#endif

	; Called at exit from fork. schedule_tail must be called to drop
	; spinlock if CONFIG_PREEMPT
ret_from_fork:
	jsr schedule_tail
	ba  ret_from_sys_call
	nop

ret_from_kernel_thread:
	jsr schedule_tail
	move.d	$r2, $r10	; argument is here
	jsr	$r1		; call the payload
	moveq	0, $r9		; no syscall restarts, TYVM...
	ba  ret_from_sys_call

ret_from_intr:
	;; check for resched if preemptive kernel or if we're going back to user-mode
	;; this test matches the user_regs(regs) macro
	;; we cannot simply test $dccr, because that does not necessarily
	;; reflect what mode we'll return into.

	move.d	[$sp + PT_dccr], $r0; regs->dccr
	btstq	8, $r0		; U-flag
	bpl     _resume_kernel
	; Note that di below is in delay slot

_resume_userspace:
	di			; so need_resched and sigpending don't change

	movs.w	-8192, $r0	; THREAD_SIZE == 8192
	and.d	$sp, $r0

	move.d	[$r0+TI_flags], $r10	; current->work
	and.d	_TIF_WORK_MASK, $r10	; is there any work to be done on return
	bne	_work_pending
	nop
	ba	_Rexit
	nop

	;; The system_call is called by a BREAK instruction, which works like
	;; an interrupt call but it stores the return PC in BRP instead of IRP.
	;; Since we dont really want to have two epilogues (one for system calls
	;; and one for interrupts) we push the contents of BRP instead of IRP in the
	;; system call prologue, to make it look like an ordinary interrupt on the
	;; stackframe.
	;;
	;; Since we can't have system calls inside interrupts, it should not matter
	;; that we don't stack IRP.
	;;
	;; In r9 we have the wanted syscall number. Arguments come in r10,r11,r12,r13,mof,srp
	;;
	;; This function looks on the _surface_ like spaghetti programming, but it's
	;; really designed so that the fast-path does not force cache-loading of non-used
	;; instructions. Only the non-common cases cause the outlined code to run..

system_call:
	;; stack-frame similar to the irq heads, which is reversed in ret_from_sys_call
	move	$brp,[$sp=$sp-16]; instruction pointer and room for a fake SBFS frame
	push	$srp
	push	$dccr
	push	$mof
	subq	14*4, $sp		; make room for r0-r13
	movem	$r13, [$sp]	; push r0-r13
	push	$r10		; push orig_r10
	clear.d [$sp=$sp-4]	; frametype == 0, normal stackframe

	movs.w	-ENOSYS, $r0
	move.d	$r0, [$sp+PT_r10]	; put the default return value in r10 in the frame

	;; check if this process is syscall-traced

	movs.w	-8192, $r0	; THREAD_SIZE == 8192
	and.d	$sp, $r0

	move.d	[$r0+TI_flags], $r0
	btstq   TIF_SYSCALL_TRACE, $r0
	bmi	_syscall_trace_entry
	nop

_syscall_traced:

	;; check for sanity in the requested syscall number

	cmpu.w	NR_syscalls, $r9
	bcc	ret_from_sys_call
	lslq	2, $r9		;  multiply by 4, in the delay slot

	;; as a bonus 7th parameter, we give the location on the stack
	;; of the register structure itself. some syscalls need this.

	push	$sp

	;; the parameter carrying registers r10, r11, r12 and 13 are intact.
	;; the fifth and sixth parameters (if any) was in mof and srp
	;; respectively, and we need to put them on the stack.

	push	$srp
	push	$mof

	jsr	[$r9+sys_call_table]	; actually do the system call
	addq	3*4, $sp		; pop the mof, srp and regs parameters
	move.d	$r10, [$sp+PT_r10]	; save the return value

	moveq	1, $r9		; "parameter" to ret_from_sys_call to show it was a sys call

	;; fall through into ret_from_sys_call to return

ret_from_sys_call:
	;; r9 is a parameter - if >=1 we came from a syscall, if 0, from an irq

	;; get the current task-struct pointer (see top for defs)

	movs.w	-8192, $r0	; THREAD_SIZE == 8192
	and.d	$sp, $r0

	di			; make sure need_resched and sigpending don't change
	move.d	[$r0+TI_flags],$r1
	and.d	_TIF_ALLWORK_MASK, $r1
	bne	_syscall_exit_work
	nop

_Rexit:
	;; this epilogue MUST match the prologues in multiple_interrupt, irq.h and ptregs.h
	pop	$r10		; frametype
	bne	_RBFexit	; was not CRIS_FRAME_NORMAL, handle otherwise
	addq	4, $sp		; skip orig_r10, in delayslot
	movem	[$sp+], $r13	; registers r0-r13
	pop	$mof		; multiply overflow register
	pop	$dccr		; condition codes
	pop	$srp		; subroutine return pointer
	;; now we have a 4-word SBFS frame which we do not want to restore
	;; using RBF since it was not stacked with SBFS. instead we would like to
	;; just get the PC value to restart it with, and skip the rest of
	;; the frame.
	;; Also notice that it's important to use instructions here that
	;; keep the interrupts disabled (since we've already popped DCCR)
	move	[$sp=$sp+16], $p8; pop the SBFS frame from the sp
	jmpu	[$sp-16]	; return through the irp field in the sbfs frame

_RBFexit:
	movem	[$sp+], $r13	; registers r0-r13, in delay slot
	pop	$mof		; multiply overflow register
	pop	$dccr		; condition codes
	pop	$srp		; subroutine return pointer
	rbf	[$sp+]		; return by popping the CPU status

	;; We get here after doing a syscall if extra work might need to be done
	;; perform syscall exit tracing if needed

_syscall_exit_work:
	;; $r0 contains current at this point and irq's are disabled

	move.d  [$r0+TI_flags], $r1
	btstq	TIF_SYSCALL_TRACE, $r1
	bpl	_work_pending
	nop

	ei

	move.d	$r9, $r1	; preserve r9
	jsr	do_syscall_trace
	move.d	$r1, $r9

	ba	_resume_userspace
	nop

_work_pending:
	move.d  [$r0+TI_flags], $r1
	btstq   TIF_NEED_RESCHED, $r1
	bpl	_work_notifysig	; was neither trace nor sched, must be signal/notify
	nop

_work_resched:
	move.d	$r9, $r1	; preserve r9
	jsr	schedule
	move.d	$r1, $r9
	di

	move.d	[$r0+TI_flags], $r1
	and.d	_TIF_WORK_MASK, $r1; ignore the syscall trace counter
	beq	_Rexit
	nop
	btstq	TIF_NEED_RESCHED, $r1
	bmi	_work_resched	; current->work.need_resched
	nop

_work_notifysig:
	;; deal with pending signals and notify-resume requests

	move.d	$r9, $r10	; do_notify_resume syscall/irq param
	move.d	$sp, $r11	; the regs param
	move.d  $r1, $r12	; the thread_info_flags parameter
	jsr	do_notify_resume

	ba _Rexit
	nop

	;; We get here as a sidetrack when we've entered a syscall with the
	;; trace-bit set. We need to call do_syscall_trace and then continue
	;; with the call.

_syscall_trace_entry:
	;; PT_r10 in the frame contains -ENOSYS as required, at this point

	jsr	do_syscall_trace

	;; now re-enter the syscall code to do the syscall itself
	;; we need to restore $r9 here to contain the wanted syscall, and
	;; the other parameter-bearing registers

	move.d	[$sp+PT_r9], $r9
	move.d	[$sp+PT_orig_r10], $r10  ; PT_r10 is already filled with -ENOSYS.
	move.d	[$sp+PT_r11],      $r11
	move.d	[$sp+PT_r12],      $r12
	move.d	[$sp+PT_r13],      $r13
	move	[$sp+PT_mof],      $mof
	move	[$sp+PT_srp],      $srp

	ba	_syscall_traced
	nop

	;; resume performs the actual task-switching, by switching stack pointers
	;; input arguments: r10 = prev, r11 = next, r12 = thread offset in task struct
	;; returns old current in r10
	;;
	;; TODO:  see the i386 version. The switch_to which calls resume in our version
	;;        could really be an inline asm of this.

resume:
	push	$srp		         ; we keep the old/new PC on the stack
	add.d	$r12, $r10		 ; r10 = current tasks tss
	move	$dccr, [$r10+THREAD_dccr]; save irq enable state
	di

	move	$usp, [$r10+ THREAD_usp] ; save user-mode stackpointer

	;; See copy_thread for the reason why register R9 is saved.
	subq	10*4, $sp
	movem	$r9, [$sp]		 ; save non-scratch registers and R9.

	move.d	$sp, [$r10+THREAD_ksp]	 ; save the kernel stack pointer for the old task
	move.d	$sp, $r10		 ; return last running task in r10
	and.d   -8192, $r10	         ; get thread_info from stackpointer
	move.d  [$r10+TI_task], $r10     ; get task
	add.d	$r12, $r11		 ; find the new tasks tss
	move.d	[$r11+THREAD_ksp], $sp	 ; switch into the new stackframe by restoring kernel sp

	movem	[$sp+], $r9		 ; restore non-scratch registers and R9.

	move	[$r11+THREAD_usp], $usp ; restore user-mode stackpointer

	move	[$r11+THREAD_dccr], $dccr ; restore irq enable status
	jump	[$sp+]		         ; restore PC

	;; This is the MMU bus fault handler.
	;; It needs to stack the CPU status and overall is different
	;; from the other interrupt handlers.

mmu_bus_fault:
	;; For refills we try to do a quick page table lookup. If it is
	;; a real fault we let the mm subsystem handle it.

	;; the first longword in the sbfs frame was the interrupted PC
	;; which fits nicely with the "IRP" slot in pt_regs normally used to
	;; contain the return address. used by Oops to print kernel errors.
	sbfs	[$sp=$sp-16]	; push the internal CPU status
	push	$dccr
	di
	subq	2*4, $sp
	movem	$r1, [$sp]
	move.d  [R_MMU_CAUSE], $r1
	;; ETRAX 100LX TR89 bugfix: if the second half of an unaligned
	;; write causes a MMU-fault, it will not be restarted correctly.
	;; This could happen if a write crosses a page-boundary and the
	;; second page is not yet COW'ed or even loaded. The workaround
	;; is to clear the unaligned bit in the CPU status record, so
	;; that the CPU will rerun both the first and second halves of
	;; the instruction. This will not have any sideeffects unless
	;; the first half goes to any device or memory that can't be
	;; written twice, and which is mapped through the MMU.
	;;
	;; We only need to do this for writes.
	btstq	8, $r1		   ; Write access?
	bpl	1f
	nop
	move.d	[$sp+16], $r0	   ; Clear unaligned bit in csrinstr
	and.d	~(1<<5), $r0
	move.d	$r0, [$sp+16]
1:	btstq	12, $r1		   ; Refill?
	bpl	2f
	lsrq	24, $r1     ; Get PGD index (bit 24-31)
	move.d  [current_pgd], $r0 ; PGD for the current process
	move.d	[$r0+$r1.d], $r0   ; Get PMD
	beq	2f
	nop
	and.w	PAGE_MASK, $r0	   ; Remove PMD flags
	move.d  [R_MMU_CAUSE], $r1
	lsrq	PAGE_SHIFT, $r1
	and.d	0x7ff, $r1         ; Get PTE index into PGD (bit 13-23)
	move.d	[$r0+$r1.d], $r1   ; Get PTE
	beq	2f
	nop
	;; Store in TLB
	move.d  $r1, [R_TLB_LO]
	;; Return
	movem	[$sp+], $r1
	pop	$dccr
	rbf	[$sp+]		; return by popping the CPU status

2:	; PMD or PTE missing, let the mm subsystem fix it up.
	movem	[$sp+], $r1
	pop	$dccr

	; Ok, not that easy, pass it on to the mm subsystem
	; The MMU status record is now on the stack
	push	$srp		; make a stackframe similar to pt_regs
	push	$dccr
	push	$mof
	di
	subq	14*4, $sp
	movem	$r13, [$sp]
	push	$r10		; dummy orig_r10
	moveq	1, $r10
	push	$r10		; frametype == 1, BUSFAULT frame type

	move.d	$sp, $r10	; pt_regs argument to handle_mmu_bus_fault

	jsr	handle_mmu_bus_fault  ; in arch/cris/arch-v10/mm/fault.c

	;; now we need to return through the normal path, we cannot just
	;; do the RBFexit since we might have killed off the running
	;; process due to a SEGV, scheduled due to a page blocking or
	;; whatever.

	moveq	0, $r9		; busfault is equivalent to an irq

	ba	ret_from_intr
	nop

	;; special handlers for breakpoint and NMI
hwbreakpoint:
	push	$dccr
	di
	push	$r10
	push	$r11
	move.d	[hw_bp_trig_ptr],$r10
	move	$brp,$r11
	move.d	$r11,[$r10+]
	move.d	$r10,[hw_bp_trig_ptr]
1:	pop	$r11
	pop	$r10
	pop	$dccr
	retb
	nop

IRQ1_interrupt:
	;; this prologue MUST match the one in irq.h and the struct in ptregs.h!!!
	move	$brp,[$sp=$sp-16]; instruction pointer and room for a fake SBFS frame
	push	$srp
	push	$dccr
	push	$mof
	di
	subq	14*4, $sp
	movem	$r13, [$sp]
	push	$r10		; push orig_r10
	clear.d [$sp=$sp-4]	; frametype == 0, normal frame

	;; If there is a glitch on the NMI pin shorter than ~100ns
	;; (i.e. non-active by the time we get here) then the nmi_pin bit
	;; in R_IRQ_MASK0_RD will already be cleared.  The watchdog_nmi bit
	;; is cleared by us however (when feeding the watchdog), which is why
	;; we use that bit to determine what brought us here.

	move.d	[R_IRQ_MASK0_RD], $r1 ; External NMI or watchdog?
	and.d   (1<<30), $r1
	bne	wdog
	move.d  $sp, $r10
	jsr	handle_nmi
	setf m			; Enable NMI again
	ba	_Rexit		; Return the standard way
	nop
wdog:
#if defined(CONFIG_ETRAX_WATCHDOG)
;; Check if we're waiting for reset to happen, as signalled by
;; hard_reset_now setting cause_of_death to a magic value.  If so, just
;; get stuck until reset happens.
	.comm	cause_of_death, 4	;; Don't declare this anywhere.
	move.d	[cause_of_death], $r10
	cmp.d	0xbedead, $r10
_killed_by_death:
	beq	_killed_by_death
	nop

;; We'll see this in ksymoops dumps.
Watchdog_bite:

#ifdef CONFIG_ETRAX_WATCHDOG_NICE_DOGGY
       ;; We just restart the watchdog here to be sure we dont get
       ;; hit while printing the watchdogmsg below
       ;; This restart is compatible with the rest of the C-code, so
       ;; the C-code can keep restarting the watchdog after this point.
       ;; The non-NICE_DOGGY code below though, disables the possibility
       ;; to restart since it changes the watchdog key, to avoid any
       ;; buggy loops etc. keeping the watchdog alive after this.
       jsr     reset_watchdog
#else

;; We need to extend the 3.3ms after the NMI at watchdog bite, so we have
;; time for an oops-dump over a 115k2 serial wire.  Another 100ms should do.

;; Change the watchdog key to an arbitrary 3-bit value and restart the
;; watchdog.
#define WD_INIT 2
	moveq	  IO_FIELD (R_WATCHDOG, key, WD_INIT), $r10
	move.d	R_WATCHDOG, $r11

	move.d	$r10, [$r11]
	moveq	  IO_FIELD (R_WATCHDOG, key,				\
			    IO_EXTRACT (R_WATCHDOG, key,		\
					IO_MASK (R_WATCHDOG, key))	\
			    ^ WD_INIT)					\
		| IO_STATE (R_WATCHDOG, enable, start), $r10
	move.d	$r10, [$r11]

#endif

;; Note that we don't do "setf m" here (or after two necessary NOPs),
;; since *not* doing that saves us from re-entrancy checks.  We don't want
;; to get here again due to possible subsequent NMIs; we want the watchdog
;; to reset us.

	move.d	_watchdogmsg,$r10
	jsr	printk

	move.d	$sp, $r10
	jsr	watchdog_bite_hook

;; This nop is here so we see the "Watchdog_bite" label in ksymoops dumps
;; rather than "spurious_interrupt".
	nop
;; At this point we drop down into spurious_interrupt, which will do a
;; hard reset.

	.section .rodata,"a"
_watchdogmsg:
	.ascii	"Oops: bitten by watchdog\n\0"
	.previous

#endif /* CONFIG_ETRAX_WATCHDOG */

spurious_interrupt:
	di
	jump hard_reset_now

	;; this handles the case when multiple interrupts arrive at the same time
	;; we jump to the first set interrupt bit in a priority fashion
	;; the hardware will call the unserved interrupts after the handler finishes

multiple_interrupt:
	;; this prologue MUST match the one in irq.h and the struct in ptregs.h!!!
	move	$irp,[$sp=$sp-16]; instruction pointer and room for a fake SBFS frame
	push	$srp
	push	$dccr
	push	$mof
	di
	subq	14*4, $sp
	movem	$r13, [$sp]
	push	$r10		; push orig_r10
	clear.d [$sp=$sp-4]	; frametype == 0, normal frame

	move.d  $sp, $r10
	jsr	do_multiple_IRQ

	jump    ret_from_intr

do_sigtrap:
	;;
	;; SIGTRAP the process that executed the break instruction.
	;; Make a frame that Rexit in entry.S expects.
	;;
	move	$brp, [$sp=$sp-16]	; Push BRP while faking a cpu status record.
	push	$srp			; Push subroutine return pointer.
	push	$dccr			; Push condition codes.
	push	$mof			; Push multiply overflow reg.
	di				; Need to disable irq's at this point.
	subq	14*4, $sp		; Make room for r0-r13.
	movem	$r13, [$sp]		; Push the r0-r13 registers.
	push	$r10			; Push orig_r10.
	clear.d	[$sp=$sp-4]		; Frametype - this is a normal stackframe.

	movs.w	-8192,$r9		; THREAD_SIZE == 8192
	and.d	$sp, $r9
	move.d  [$r9+TI_task], $r10
	move.d  [$r10+TASK_pid], $r10	; current->pid as arg1.
	moveq	5, $r11			; SIGTRAP as arg2.
	jsr	sys_kill
	jump	ret_from_intr		; Use the return routine for interrupts.

gdb_handle_breakpoint:
	push	$dccr
	push	$r0
#ifdef CONFIG_ETRAX_KGDB
	move	$dccr, $r0		; U-flag not affected by previous insns.
	btstq	8, $r0			; Test the U-flag.
	bmi	_ugdb_handle_breakpoint	; Go to user mode debugging.
	nop				; Empty delay slot (cannot pop r0 here).
	pop	$r0			; Restore r0.
	ba	kgdb_handle_breakpoint	; Go to kernel debugging.
	pop	$dccr			; Restore dccr in delay slot.
#endif

_ugdb_handle_breakpoint:
	move	$brp, $r0		; Use r0 temporarily for calculation.
	subq	2, $r0			; Set to address of previous instruction.
	move	$r0, $brp
	pop	$r0			; Restore r0.
	ba	do_sigtrap		; SIGTRAP the offending process.
	pop	$dccr			; Restore dccr in delay slot.

	.data

hw_bp_trigs:
	.space 64*4
hw_bp_trig_ptr:
	.dword hw_bp_trigs

	.section .rodata,"a"
sys_call_table:
	.long sys_restart_syscall	/* 0 - old "setup()" system call, used for restarting */
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
	.long sys_old_readdir
	.long sys_old_mmap	/* 90 */
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
	.long sys_ni_syscall	/* 110 */ /* sys_iopl in i386 */
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
	.long sys_ni_syscall    /* old nfsservctl */
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
	.long sys_mmap2		/* mmap_pgoff */
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
	.long sys_tgkill		/* 270 */
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
	.long sys_ni_syscall
	.long sys_waitid
	.long sys_ni_syscall		/* 285 */ /* available */
	.long sys_add_key
	.long sys_request_key
	.long sys_keyctl
	.long sys_ioprio_set
	.long sys_ioprio_get		/* 290 */
	.long sys_inotify_init
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
	.long sys_migrate_pages
	.long sys_openat		/* 295 */
	.long sys_mkdirat
	.long sys_mknodat
	.long sys_fchownat
	.long sys_futimesat
	.long sys_fstatat64		/* 300 */
	.long sys_unlinkat
	.long sys_renameat
	.long sys_linkat
	.long sys_symlinkat
	.long sys_readlinkat		/* 305 */
	.long sys_fchmodat
	.long sys_faccessat
	.long sys_pselect6
	.long sys_ppoll
	.long sys_unshare		/* 310 */
	.long sys_set_robust_list
	.long sys_get_robust_list
	.long sys_splice
	.long sys_sync_file_range
	.long sys_tee			/* 315 */
	.long sys_vmsplice
	.long sys_move_pages
	.long sys_getcpu
	.long sys_epoll_pwait
	.long sys_utimensat		/* 320 */
	.long sys_signalfd
	.long sys_timerfd_create
	.long sys_eventfd
	.long sys_fallocate
	.long sys_timerfd_settime	/* 325 */
	.long sys_timerfd_gettime
	.long sys_signalfd4
	.long sys_eventfd2
	.long sys_epoll_create1
	.long sys_dup3			/* 330 */
	.long sys_pipe2
	.long sys_inotify_init1
	.long sys_preadv
	.long sys_pwritev
	.long sys_setns			/* 335 */
	.long sys_name_to_handle_at
	.long sys_open_by_handle_at
	.long sys_rt_tgsigqueueinfo
	.long sys_perf_event_open
	.long sys_recvmmsg		/* 340 */
	.long sys_accept4
	.long sys_fanotify_init
	.long sys_fanotify_mark
	.long sys_prlimit64
	.long sys_clock_adjtime		/* 345 */
	.long sys_syncfs
	.long sys_sendmmsg
	.long sys_process_vm_readv
	.long sys_process_vm_writev
	.long sys_kcmp			/* 350 */
	.long sys_finit_module
	.long sys_sched_setattr
	.long sys_sched_getattr
	.long sys_renameat2
	.long sys_seccomp		/* 355 */
	.long sys_getrandom
	.long sys_memfd_create
	.long sys_bpf
	.long sys_execveat

        /*
         * NOTE!! This doesn't have to be exact - we just have
         * to make sure we have _enough_ of the "sys_ni_syscall"
         * entries. Don't panic if you notice that this hasn't
         * been shrunk every time we add a new system call.
         */

	.rept NR_syscalls-(.-sys_call_table)/4
		.long sys_ni_syscall
	.endr

