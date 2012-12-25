/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 - 2000, 2001 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2004 Thiemo Seufer
 *
 * Hairy, the userspace application uses a different argument passing
 * convention than the kernel, so we have to translate things from o32
 * to ABI64 calling convention.  64-bit syscalls are also processed
 * here for now.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/sysmips.h>

	.align  5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at
	ld	t1, PT_EPC(sp)		# skip syscall on return

	dsubu	t0, v0, __NR_O32_Linux	# check syscall number
	sltiu	t0, t0, __NR_O32_Linux_syscalls + 1
	daddiu	t1, 4			# skip to next instruction
	sd	t1, PT_EPC(sp)
	beqz	t0, not_o32_scall
#if 0
 SAVE_ALL
 move a1, v0
 PRINT("Scall %ld\n")
 RESTORE_ALL
#endif

	/* We don't want to stumble over broken sign extensions from
	   userland. O32 does never use the upper half. */
	sll	a0, a0, 0
	sll	a1, a1, 0
	sll	a2, a2, 0
	sll	a3, a3, 0

	dsll	t0, v0, 3		# offset into table
	ld	t2, (sys_call_table - (__NR_O32_Linux * 8))(t0)

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting

	/*
	 * More than four arguments.  Try to deal with it by copying the
	 * stack arguments from the user stack to the kernel stack.
	 * This Sucks (TM).
	 *
	 * We intentionally keep the kernel stack a little below the top of
	 * userspace so we don't have to do a slower byte accurate check here.
	 */
	ld	t0, PT_R29(sp)		# get old user stack pointer
	daddu	t1, t0, 32
	bltz	t1, bad_stack

1:	lw	a4, 16(t0)		# argument #5 from usp
2:	lw	a5, 20(t0)		# argument #6 from usp
3:	lw	a6, 24(t0)		# argument #7 from usp
4:	lw	a7, 28(t0)		# argument #8 from usp (for indirect syscalls)

	.section __ex_table,"a"
	PTR	1b, bad_stack
	PTR	2b, bad_stack
	PTR	3b, bad_stack
	PTR	4b, bad_stack
	.previous

	li	t1, _TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT
	LONG_L	t0, TI_FLAGS($28)	# syscall tracing enabled?
	and	t0, t1, t0
	bnez	t0, trace_a_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	ld	t1, PT_R2(sp)		# syscall number
	dnegu	v0			# error
	sd	t1, PT_R0(sp)		# save it for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

o32_syscall_exit:
	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

trace_a_syscall:
	SAVE_STATIC
	sd	a4, PT_R8(sp)		# Save argument registers
	sd	a5, PT_R9(sp)
	sd	a6, PT_R10(sp)
	sd	a7, PT_R11(sp)		# For indirect syscalls

	move	s0, t2			# Save syscall pointer
	move	a0, sp
	jal	syscall_trace_enter

	move	t0, s0
	RESTORE_STATIC
	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	ld	a4, PT_R8(sp)
	ld	a5, PT_R9(sp)
	ld	a6, PT_R10(sp)
	ld	a7, PT_R11(sp)		# For indirect syscalls
	jalr	t0

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sd	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	ld	t1, PT_R2(sp)		# syscall number
	dnegu	v0			# error
	sd	t1, PT_R0(sp)		# save it for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	j	syscall_exit

/* ------------------------------------------------------------------------ */

	/*
	 * The stackpointer for a call with more than 4 arguments is bad.
	 */
bad_stack:
	li	v0, EFAULT
	sd	v0, PT_R2(sp)
	li	t0, 1			# set error flag
	sd	t0, PT_R7(sp)
	j	o32_syscall_exit

not_o32_scall:
	/*
	 * This is not an o32 compatibility syscall, pass it on
	 * to the 64-bit syscall handlers.
	 */
#ifdef CONFIG_MIPS32_N32
	j	handle_sysn32
#else
	j	handle_sys64
#endif
	END(handle_sys)

LEAF(sys32_syscall)
	subu	t0, a0, __NR_O32_Linux	# check syscall number
	sltiu	v0, t0, __NR_O32_Linux_syscalls + 1
	beqz	t0, einval		# do not recurse
	dsll	t1, t0, 3
	beqz	v0, einval
	ld	t2, sys_call_table(t1)		# syscall routine

	move	a0, a1			# shift argument registers
	move	a1, a2
	move	a2, a3
	move	a3, a4
	move	a4, a5
	move	a5, a6
	move	a6, a7
	sd	a0, PT_R4(sp)		# ... and push back a0 - a3, some
	sd	a1, PT_R5(sp)		# syscalls expect them there
	sd	a2, PT_R6(sp)
	sd	a3, PT_R7(sp)
	sd	a3, PT_R26(sp)		# update a3 for syscall restarting
	jr	t2
	/* Unreached */

einval:	li	v0, -ENOSYS
	jr	ra
	END(sys32_syscall)

	.align	3
	.type	sys_call_table,@object
sys_call_table:
	PTR	sys32_syscall			/* 4000 */
	PTR	sys_exit
	PTR	sys_fork
	PTR	sys_read
	PTR	sys_write
	PTR	compat_sys_open			/* 4005 */
	PTR	sys_close
	PTR	sys_waitpid
	PTR	sys_creat
	PTR	sys_link
	PTR	sys_unlink			/* 4010 */
	PTR	compat_sys_execve
	PTR	sys_chdir
	PTR	compat_sys_time
	PTR	sys_mknod
	PTR	sys_chmod			/* 4015 */
	PTR	sys_lchown
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall			/* was sys_stat */
	PTR	sys_lseek
	PTR	sys_getpid			/* 4020 */
	PTR	compat_sys_mount
	PTR	sys_oldumount
	PTR	sys_setuid
	PTR	sys_getuid
	PTR	compat_sys_stime		/* 4025 */
	PTR	compat_sys_ptrace
	PTR	sys_alarm
	PTR	sys_ni_syscall			/* was sys_fstat */
	PTR	sys_pause
	PTR	compat_sys_utime		/* 4030 */
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_access
	PTR	sys_nice
	PTR	sys_ni_syscall			/* 4035 */
	PTR	sys_sync
	PTR	sys_kill
	PTR	sys_rename
	PTR	sys_mkdir
	PTR	sys_rmdir			/* 4040 */
	PTR	sys_dup
	PTR	sysm_pipe
	PTR	compat_sys_times
	PTR	sys_ni_syscall
	PTR	sys_brk				/* 4045 */
	PTR	sys_setgid
	PTR	sys_getgid
	PTR	sys_ni_syscall			/* was signal	2 */
	PTR	sys_geteuid
	PTR	sys_getegid			/* 4050 */
	PTR	sys_acct
	PTR	sys_umount
	PTR	sys_ni_syscall
	PTR	compat_sys_ioctl
	PTR	compat_sys_fcntl		/* 4055 */
	PTR	sys_ni_syscall
	PTR	sys_setpgid
	PTR	sys_ni_syscall
	PTR	sys_olduname
	PTR	sys_umask			/* 4060 */
	PTR	sys_chroot
	PTR	compat_sys_ustat
	PTR	sys_dup2
	PTR	sys_getppid
	PTR	sys_getpgrp			/* 4065 */
	PTR	sys_setsid
	PTR	sys_32_sigaction
	PTR	sys_sgetmask
	PTR	sys_ssetmask
	PTR	sys_setreuid			/* 4070 */
	PTR	sys_setregid
	PTR	sys32_sigsuspend
	PTR	compat_sys_sigpending
	PTR	sys_sethostname
	PTR	compat_sys_setrlimit		/* 4075 */
	PTR	compat_sys_getrlimit
	PTR	compat_sys_getrusage
	PTR	compat_sys_gettimeofday
	PTR	compat_sys_settimeofday
	PTR	sys_getgroups			/* 4080 */
	PTR	sys_setgroups
	PTR	sys_ni_syscall			/* old_select */
	PTR	sys_symlink
	PTR	sys_ni_syscall			/* was sys_lstat */
	PTR	sys_readlink			/* 4085 */
	PTR	sys_uselib
	PTR	sys_swapon
	PTR	sys_reboot
	PTR	compat_sys_old_readdir
	PTR	sys_mips_mmap			/* 4090 */
	PTR	sys_munmap
	PTR	sys_truncate
	PTR	sys_ftruncate
	PTR	sys_fchmod
	PTR	sys_fchown			/* 4095 */
	PTR	sys_getpriority
	PTR	sys_setpriority
	PTR	sys_ni_syscall
	PTR	compat_sys_statfs
	PTR	compat_sys_fstatfs		/* 4100 */
	PTR	sys_ni_syscall			/* sys_ioperm */
	PTR	compat_sys_socketcall
	PTR	sys_syslog
	PTR	compat_sys_setitimer
	PTR	compat_sys_getitimer		/* 4105 */
	PTR	compat_sys_newstat
	PTR	compat_sys_newlstat
	PTR	compat_sys_newfstat
	PTR	sys_uname
	PTR	sys_ni_syscall			/* sys_ioperm  *//* 4110 */
	PTR	sys_vhangup
	PTR	sys_ni_syscall			/* was sys_idle	 */
	PTR	sys_ni_syscall			/* sys_vm86 */
	PTR	compat_sys_wait4
	PTR	sys_swapoff			/* 4115 */
	PTR	compat_sys_sysinfo
	PTR	sys_32_ipc
	PTR	sys_fsync
	PTR	sys32_sigreturn
	PTR	sys32_clone			/* 4120 */
	PTR	sys_setdomainname
	PTR	sys_newuname
	PTR	sys_ni_syscall			/* sys_modify_ldt */
	PTR	compat_sys_adjtimex
	PTR	sys_mprotect			/* 4125 */
	PTR	compat_sys_sigprocmask
	PTR	sys_ni_syscall			/* was creat_module */
	PTR	sys_init_module
	PTR	sys_delete_module
	PTR	sys_ni_syscall			/* 4130, get_kernel_syms */
	PTR	sys_quotactl
	PTR	sys_getpgid
	PTR	sys_fchdir
	PTR	sys_bdflush
	PTR	sys_sysfs			/* 4135 */
	PTR	sys_32_personality
	PTR	sys_ni_syscall	 		/* for afs_syscall */
	PTR	sys_setfsuid
	PTR	sys_setfsgid
	PTR	sys_32_llseek			/* 4140 */
	PTR	compat_sys_getdents
	PTR	compat_sys_select
	PTR	sys_flock
	PTR	sys_msync
	PTR	compat_sys_readv		/* 4145 */
	PTR	compat_sys_writev
	PTR	sys_cacheflush
	PTR	sys_cachectl
	PTR	sys_sysmips
	PTR	sys_ni_syscall			/* 4150 */
	PTR	sys_getsid
	PTR	sys_fdatasync
	PTR	compat_sys_sysctl
	PTR	sys_mlock
	PTR	sys_munlock			/* 4155 */
	PTR	sys_mlockall
	PTR	sys_munlockall
	PTR	sys_sched_setparam
	PTR	sys_sched_getparam
	PTR	sys_sched_setscheduler 		/* 4160 */
	PTR	sys_sched_getscheduler
	PTR	sys_sched_yield
	PTR	sys_sched_get_priority_max
	PTR	sys_sched_get_priority_min
	PTR	sys_32_sched_rr_get_interval 	/* 4165 */
	PTR	compat_sys_nanosleep
	PTR	sys_mremap
	PTR	sys_accept
	PTR	sys_bind
	PTR	sys_connect			/* 4170 */
	PTR	sys_getpeername
	PTR	sys_getsockname
	PTR	sys_getsockopt
	PTR	sys_listen
	PTR	compat_sys_recv			/* 4175 */
	PTR	compat_sys_recvfrom
	PTR	compat_sys_recvmsg
	PTR	sys_send
	PTR	compat_sys_sendmsg
	PTR	sys_sendto			/* 4180 */
	PTR	compat_sys_setsockopt
	PTR	sys_shutdown
	PTR	sys_socket
	PTR	sys_socketpair
	PTR	sys_setresuid			/* 4185 */
	PTR	sys_getresuid
	PTR	sys_ni_syscall			/* was query_module */
	PTR	sys_poll
	PTR	sys_ni_syscall			/* was nfsservctl */
	PTR	sys_setresgid			/* 4190 */
	PTR	sys_getresgid
	PTR	sys_prctl
	PTR	sys32_rt_sigreturn
	PTR	sys_32_rt_sigaction
	PTR	compat_sys_rt_sigprocmask 	/* 4195 */
	PTR	sys_32_rt_sigpending
	PTR	compat_sys_rt_sigtimedwait
	PTR	sys_32_rt_sigqueueinfo
	PTR	compat_sys_rt_sigsuspend
	PTR	sys_32_pread			/* 4200 */
	PTR	sys_32_pwrite
	PTR	sys_chown
	PTR	sys_getcwd
	PTR	sys_capget
	PTR	sys_capset			/* 4205 */
	PTR	compat_sys_sigaltstack
	PTR	sys_32_sendfile
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_mips_mmap2			/* 4210 */
	PTR	sys_32_truncate64
	PTR	sys_32_ftruncate64
	PTR	sys_newstat
	PTR	sys_newlstat
	PTR	sys_newfstat			/* 4215 */
	PTR	sys_pivot_root
	PTR	sys_mincore
	PTR	sys_madvise
	PTR	sys_getdents64
	PTR	compat_sys_fcntl64		/* 4220 */
	PTR	sys_ni_syscall
	PTR	sys_gettid
	PTR	sys32_readahead
	PTR	sys_setxattr
	PTR	sys_lsetxattr			/* 4225 */
	PTR	sys_fsetxattr
	PTR	sys_getxattr
	PTR	sys_lgetxattr
	PTR	sys_fgetxattr
	PTR	sys_listxattr			/* 4230 */
	PTR	sys_llistxattr
	PTR	sys_flistxattr
	PTR	sys_removexattr
	PTR	sys_lremovexattr
	PTR	sys_fremovexattr		/* 4235 */
	PTR	sys_tkill
	PTR	sys_sendfile64
	PTR	sys_32_futex
	PTR	compat_sys_sched_setaffinity
	PTR	compat_sys_sched_getaffinity	/* 4240 */
	PTR	compat_sys_io_setup
	PTR	sys_io_destroy
	PTR	compat_sys_io_getevents
	PTR	compat_sys_io_submit
	PTR	sys_io_cancel			/* 4245 */
	PTR	sys_exit_group
	PTR	sys32_lookup_dcookie
	PTR	sys_epoll_create
	PTR	sys_epoll_ctl
	PTR	sys_epoll_wait			/* 4250 */
	PTR	sys_remap_file_pages
	PTR	sys_set_tid_address
	PTR	sys_restart_syscall
	PTR	sys32_fadvise64_64
	PTR	compat_sys_statfs64		/* 4255 */
	PTR	compat_sys_fstatfs64
	PTR	compat_sys_timer_create
	PTR	compat_sys_timer_settime
	PTR	compat_sys_timer_gettime
	PTR	sys_timer_getoverrun		/* 4260 */
	PTR	sys_timer_delete
	PTR	compat_sys_clock_settime
	PTR	compat_sys_clock_gettime
	PTR	compat_sys_clock_getres
	PTR	compat_sys_clock_nanosleep	/* 4265 */
	PTR	sys_tgkill
	PTR	compat_sys_utimes
	PTR	sys_ni_syscall			/* sys_mbind */
	PTR	sys_ni_syscall			/* sys_get_mempolicy */
	PTR	sys_ni_syscall			/* 4270 sys_set_mempolicy */
	PTR	compat_sys_mq_open
	PTR	sys_mq_unlink
	PTR	compat_sys_mq_timedsend
	PTR	compat_sys_mq_timedreceive
	PTR	compat_sys_mq_notify		/* 4275 */
	PTR	compat_sys_mq_getsetattr
	PTR	sys_ni_syscall			/* sys_vserver */
	PTR	compat_sys_waitid
	PTR	sys_ni_syscall			/* available, was setaltroot */
	PTR	sys_add_key			/* 4280 */
	PTR	sys_request_key
	PTR	sys_keyctl
	PTR	sys_set_thread_area
	PTR	sys_inotify_init
	PTR	sys_inotify_add_watch		/* 4285 */
	PTR	sys_inotify_rm_watch
	PTR	sys_migrate_pages
	PTR	compat_sys_openat
	PTR	sys_mkdirat
	PTR	sys_mknodat			/* 4290 */
	PTR	sys_fchownat
	PTR	compat_sys_futimesat
	PTR	sys_newfstatat
	PTR	sys_unlinkat
	PTR	sys_renameat			/* 4295 */
	PTR	sys_linkat
	PTR	sys_symlinkat
	PTR	sys_readlinkat
	PTR	sys_fchmodat
	PTR	sys_faccessat			/* 4300 */
	PTR	compat_sys_pselect6
	PTR	compat_sys_ppoll
	PTR	sys_unshare
	PTR	sys_splice
	PTR	sys32_sync_file_range		/* 4305 */
	PTR	sys_tee
	PTR	compat_sys_vmsplice
	PTR	compat_sys_move_pages
	PTR	compat_sys_set_robust_list
	PTR	compat_sys_get_robust_list	/* 4310 */
	PTR	compat_sys_kexec_load
	PTR	sys_getcpu
	PTR	compat_sys_epoll_pwait
	PTR	sys_ioprio_set
	PTR	sys_ioprio_get			/* 4315 */
	PTR	compat_sys_utimensat
	PTR	compat_sys_signalfd
	PTR	sys_ni_syscall			/* was timerfd */
	PTR	sys_eventfd
	PTR	sys32_fallocate			/* 4320 */
	PTR	sys_timerfd_create
	PTR	compat_sys_timerfd_gettime
	PTR	compat_sys_timerfd_settime
	PTR	compat_sys_signalfd4
	PTR	sys_eventfd2			/* 4325 */
	PTR	sys_epoll_create1
	PTR	sys_dup3
	PTR	sys_pipe2
	PTR	sys_inotify_init1
	PTR	compat_sys_preadv		/* 4330 */
	PTR	compat_sys_pwritev
	PTR	compat_sys_rt_tgsigqueueinfo
	PTR	sys_perf_event_open
	PTR	sys_accept4
	PTR	compat_sys_recvmmsg		/* 4335 */
	PTR	sys_fanotify_init
	PTR	sys_32_fanotify_mark
	PTR	sys_prlimit64
	PTR	sys_name_to_handle_at
	PTR	compat_sys_open_by_handle_at	/* 4340 */
	PTR	compat_sys_clock_adjtime
	PTR	sys_syncfs
	PTR	compat_sys_sendmmsg
	PTR	sys_setns
	PTR	compat_sys_process_vm_readv	/* 4345 */
	PTR	compat_sys_process_vm_writev
	PTR	sys_kcmp
	PTR	sys_finit_module
	.size	sys_call_table,.-sys_call_table
