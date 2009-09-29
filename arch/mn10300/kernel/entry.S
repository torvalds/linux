###############################################################################
#
# MN10300 Exception and interrupt entry points
#
# Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
# Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
# Modified by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public Licence
# as published by the Free Software Foundation; either version
# 2 of the Licence, or (at your option) any later version.
#
###############################################################################
#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/thread_info.h>
#include <asm/intctl-regs.h>
#include <asm/busctl-regs.h>
#include <asm/timer-regs.h>
#include <unit/leds.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/frame.inc>

#ifdef CONFIG_PREEMPT
#define preempt_stop		__cli
#else
#define preempt_stop
#define resume_kernel		restore_all
#endif

	.macro __cli
	and	~EPSW_IM,epsw
	or	EPSW_IE|MN10300_CLI_LEVEL,epsw
	nop
	nop
	nop
	.endm
	.macro __sti
	or	EPSW_IE|EPSW_IM_7,epsw
	.endm


	.am33_2

###############################################################################
#
# the return path for a forked child
# - on entry, D0 holds the address of the previous task to run
#
###############################################################################
ENTRY(ret_from_fork)
	call	schedule_tail[],0
	GET_THREAD_INFO a2

	# return 0 to indicate child process
	clr	d0
	mov	d0,(REG_D0,fp)
	jmp	syscall_exit

###############################################################################
#
# system call handler
#
###############################################################################
ENTRY(system_call)
	add	-4,sp
	SAVE_ALL
	mov	d0,(REG_ORIG_D0,fp)
	GET_THREAD_INFO a2
	cmp	nr_syscalls,d0
	bcc	syscall_badsys
	btst	_TIF_SYSCALL_TRACE,(TI_flags,a2)
	bne	syscall_entry_trace
syscall_call:
	add	d0,d0,a1
	add	a1,a1
	mov	(REG_A0,fp),d0
	mov	(sys_call_table,a1),a0
	calls	(a0)
	mov	d0,(REG_D0,fp)
syscall_exit:
	# make sure we don't miss an interrupt setting need_resched or
	# sigpending between sampling and the rti
	__cli
	mov	(TI_flags,a2),d2
	btst	_TIF_ALLWORK_MASK,d2
	bne	syscall_exit_work
restore_all:
	RESTORE_ALL

###############################################################################
#
# perform work that needs to be done immediately before resumption and syscall
# tracing
#
###############################################################################
	ALIGN
syscall_exit_work:
	btst	_TIF_SYSCALL_TRACE,d2
	beq	work_pending
	__sti				# could let syscall_trace_exit() call
					# schedule() instead
	mov	fp,d0
	call	syscall_trace_exit[],0	# do_syscall_trace(regs)
	jmp	resume_userspace

	ALIGN
work_pending:
	btst	_TIF_NEED_RESCHED,d2
	beq	work_notifysig

work_resched:
	call	schedule[],0

	# make sure we don't miss an interrupt setting need_resched or
	# sigpending between sampling and the rti
	__cli

	# is there any work to be done other than syscall tracing?
	mov	(TI_flags,a2),d2
	btst	_TIF_WORK_MASK,d2
	beq	restore_all
	btst	_TIF_NEED_RESCHED,d2
	bne	work_resched

	# deal with pending signals and notify-resume requests
work_notifysig:
	mov	fp,d0
	mov	d2,d1
	call	do_notify_resume[],0
	jmp	resume_userspace

	# perform syscall entry tracing
syscall_entry_trace:
	mov	-ENOSYS,d0
	mov	d0,(REG_D0,fp)
	mov	fp,d0
	call	syscall_trace_entry[],0	# returns the syscall number to actually use
	mov	(REG_D1,fp),d1
	cmp	nr_syscalls,d0
	bcs	syscall_call
	jmp	syscall_exit

syscall_badsys:
	mov	-ENOSYS,d0
	mov	d0,(REG_D0,fp)
	jmp	resume_userspace

	# userspace resumption stub bypassing syscall exit tracing
	.globl	ret_from_exception, ret_from_intr
	ALIGN
ret_from_exception:
	preempt_stop
ret_from_intr:
	GET_THREAD_INFO a2
	mov	(REG_EPSW,fp),d0	# need to deliver signals before
					# returning to userspace
	and	EPSW_nSL,d0
	beq	resume_kernel		# returning to supervisor mode

ENTRY(resume_userspace)
	# make sure we don't miss an interrupt setting need_resched or
	# sigpending between sampling and the rti
	__cli

	# is there any work to be done on int/exception return?
	mov	(TI_flags,a2),d2
	btst	_TIF_WORK_MASK,d2
	bne	work_pending
	jmp	restore_all

#ifdef CONFIG_PREEMPT
ENTRY(resume_kernel)
	__cli
	mov	(TI_preempt_count,a2),d0	# non-zero preempt_count ?
	cmp	0,d0
	bne	restore_all

need_resched:
	btst	_TIF_NEED_RESCHED,(TI_flags,a2)
	beq	restore_all
	mov	(REG_EPSW,fp),d0
	and	EPSW_IM,d0
	cmp	EPSW_IM_7,d0		# interrupts off (exception path) ?
	bne	restore_all
	call	preempt_schedule_irq[],0
	jmp	need_resched
#endif


###############################################################################
#
# IRQ handler entry point
# - intended to be entered at multiple priorities
#
###############################################################################
ENTRY(irq_handler)
	add	-4,sp
	SAVE_ALL

	# it's not a syscall
	mov	0xffffffff,d0
	mov	d0,(REG_ORIG_D0,fp)

	mov	fp,d0
	call	do_IRQ[],0			# do_IRQ(regs)

	jmp	ret_from_intr

###############################################################################
#
# Monitor Signal handler entry point
#
###############################################################################
ENTRY(monitor_signal)
	movbu	(0xae000001),d1
	cmp	1,d1
	beq	monsignal
	ret	[],0

monsignal:
	or	EPSW_NMID,epsw
	mov	d0,a0
	mov	a0,sp
	mov	(REG_EPSW,fp),d1
	and	~EPSW_nSL,d1
	mov	d1,(REG_EPSW,fp)
	movm	(sp),[d2,d3,a2,a3,exreg0,exreg1,exother]
	mov	(sp),a1
	mov	a1,usp
	movm	(sp),[other]
	add	4,sp
here:	jmp	0x8e000008-here+0x8e000008

###############################################################################
#
# Double Fault handler entry point
# - note that there will not be a stack, D0/A0 will hold EPSW/PC as were
#
###############################################################################
	.section .bss
	.balign	THREAD_SIZE
	.space	THREAD_SIZE
__df_stack:
	.previous

ENTRY(double_fault)
	mov	a0,(__df_stack-4)	# PC as was
	mov	d0,(__df_stack-8)	# EPSW as was
	mn10300_set_dbfleds		# display 'db-f' on the LEDs
	mov	0xaa55aa55,d0
	mov	d0,(__df_stack-12)	# no ORIG_D0
	mov	sp,a0			# save corrupted SP
	mov	__df_stack-12,sp	# emergency supervisor stack
	SAVE_ALL
	mov	a0,(REG_A0,fp)		# save corrupted SP as A0 (which got
					# clobbered by the CPU)
	mov	fp,d0
	calls	do_double_fault
double_fault_loop:
	bra	double_fault_loop

###############################################################################
#
# Bus Error handler entry point
# - handle external (async) bus errors separately
#
###############################################################################
ENTRY(raw_bus_error)
	add	-4,sp
	mov	d0,(sp)
	mov	(BCBERR),d0		# what
	btst	BCBERR_BEMR_DMA,d0	# see if it was an external bus error
	beq	__common_exception_aux	# it wasn't

	SAVE_ALL
	mov	(BCBEAR),d1		# destination of erroneous access

	mov	(REG_ORIG_D0,fp),d2
	mov	d2,(REG_D0,fp)
	mov	-1,d2
	mov	d2,(REG_ORIG_D0,fp)

	add	-4,sp
	mov	fp,(12,sp)		# frame pointer
	call	io_bus_error[],0
	jmp	restore_all

###############################################################################
#
# Miscellaneous exception entry points
#
###############################################################################
ENTRY(nmi_handler)
	add	-4,sp
	mov	d0,(sp)
	mov	(TBR),d0
	bra	__common_exception_nonmi

ENTRY(__common_exception)
	add	-4,sp
	mov	d0,(sp)

__common_exception_aux:
	mov	(TBR),d0
	and	~EPSW_NMID,epsw		# turn NMIs back on if not NMI
	or	EPSW_IE,epsw

__common_exception_nonmi:
	and	0x0000FFFF,d0		# turn the exception code into a vector
					# table index

	btst	0x00000007,d0
	bne	1f
	cmp	0x00000400,d0
	bge	1f

	SAVE_ALL			# build the stack frame

	mov	(REG_D0,fp),a2		# get the exception number
	mov	(REG_ORIG_D0,fp),d0
	mov	d0,(REG_D0,fp)
	mov	-1,d0
	mov	d0,(REG_ORIG_D0,fp)

#ifdef CONFIG_GDBSTUB
	btst	0x01,(gdbstub_busy)
	beq	2f
	and	~EPSW_IE,epsw
	mov	fp,d0
	mov	a2,d1
	call	gdbstub_exception[],0	# gdbstub itself caused an exception
	bra	restore_all
2:
#endif

	mov	fp,d0			# arg 0: stacked register file
	mov	a2,d1			# arg 1: exception number
	lsr	1,a2

	mov	(exception_table,a2),a2
	calls	(a2)
	jmp	ret_from_exception

1:	pi				# BUG() equivalent

###############################################################################
#
# Exception handler functions table
#
###############################################################################
	.data
ENTRY(exception_table)
	.rept	0x400>>1
	 .long	uninitialised_exception
	.endr
	.previous

###############################################################################
#
# Change an entry in the exception table
# - D0 exception code, D1 handler
#
###############################################################################
ENTRY(set_excp_vector)
	lsr	1,d0
	add	exception_table,d0
	mov	d1,(d0)
	mov	4,d1
#if defined(CONFIG_MN10300_CACHE_WBACK)
	jmp	mn10300_dcache_flush_inv_range2
#else
	ret	[],0
#endif

###############################################################################
#
# System call table
#
###############################################################################
	.data
ENTRY(sys_call_table)
	.long sys_restart_syscall	/* 0 */
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
	.long sys_ni_syscall	/* 35 - old ftime syscall holder */
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
	.long sys_umount	/* recycled never used phys() */
	.long sys_ni_syscall	/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall	/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall	/* old ulimit syscall holder */
	.long sys_ni_syscall	/* old sys_olduname */
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
	.long old_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_old_readdir
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
	.long sys_ni_syscall	/* ioperm */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall	/* old sys_uname */
	.long sys_ni_syscall	/* 110 - iopl */
	.long sys_vhangup
	.long sys_ni_syscall	/* old "idle" system call */
	.long sys_ni_syscall	/* vm86old */
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall	/* modify_ldt */
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
	.long sys_ni_syscall	/* reserved for afs_syscall */
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
	.long sys_sched_getparam   /* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid16
	.long sys_getresuid16	/* 165 */
	.long sys_ni_syscall	/* vm86 */
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
	.long sys_ni_syscall	/* reserved for streams1 */
	.long sys_ni_syscall	/* reserved for streams2 */
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
	.long sys_tgkill		/* 270 */
	.long sys_utimes
 	.long sys_fadvise64_64
	.long sys_ni_syscall	/* sys_vserver */
	.long sys_mbind
	.long sys_get_mempolicy		/* 275 */
	.long sys_set_mempolicy
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive	/* 280 */
	.long sys_mq_notify
	.long sys_mq_getsetattr
	.long sys_kexec_load
	.long sys_waitid
	.long sys_ni_syscall		/* 285 */ /* available */
	.long sys_add_key
	.long sys_request_key
	.long sys_keyctl
	.long sys_cacheflush
	.long sys_ioprio_set		/* 290 */
	.long sys_ioprio_get
	.long sys_inotify_init
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
	.long sys_migrate_pages		/* 295 */
	.long sys_openat
	.long sys_mkdirat
	.long sys_mknodat
	.long sys_fchownat
	.long sys_futimesat		/* 300 */
	.long sys_fstatat64
	.long sys_unlinkat
	.long sys_renameat
	.long sys_linkat
	.long sys_symlinkat		/* 305 */
	.long sys_readlinkat
	.long sys_fchmodat
	.long sys_faccessat
	.long sys_pselect6
	.long sys_ppoll			/* 310 */
	.long sys_unshare
	.long sys_set_robust_list
	.long sys_get_robust_list
	.long sys_splice
	.long sys_sync_file_range	/* 315 */
	.long sys_tee
	.long sys_vmsplice
	.long sys_move_pages
	.long sys_getcpu
	.long sys_epoll_pwait		/* 320 */
	.long sys_utimensat
	.long sys_signalfd
	.long sys_timerfd_create
	.long sys_eventfd
	.long sys_fallocate		/* 325 */
	.long sys_timerfd_settime
	.long sys_timerfd_gettime
	.long sys_signalfd4
	.long sys_eventfd2
	.long sys_epoll_create1		/* 330 */
	.long sys_dup3
	.long sys_pipe2
	.long sys_inotify_init1
	.long sys_preadv
	.long sys_pwritev		/* 335 */
	.long sys_rt_tgsigqueueinfo
	.long sys_perf_event_open


nr_syscalls=(.-sys_call_table)/4
