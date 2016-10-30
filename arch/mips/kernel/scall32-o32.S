/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995-99, 2000- 02, 06 Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2004 Thiemo Seufer
 * Copyright (C) 2014 Imagination Technologies Ltd.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/isadep.h>
#include <asm/sysmips.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>
#include <asm/war.h>
#include <asm/asm-offsets.h>

/* Highest syscall used of any syscall flavour */
#define MAX_SYSCALL_NO	__NR_O32_Linux + __NR_O32_Linux_syscalls

	.align	5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
	SAVE_SOME
	TRACE_IRQS_ON_RELOAD
	STI
	.set	at

	lw	t1, PT_EPC(sp)		# skip syscall on return

	addiu	t1, 4			# skip to next instruction
	sw	t1, PT_EPC(sp)

	sw	a3, PT_R26(sp)		# save a3 for syscall restarting

	/*
	 * More than four arguments.  Try to deal with it by copying the
	 * stack arguments from the user stack to the kernel stack.
	 * This Sucks (TM).
	 */
	lw	t0, PT_R29(sp)		# get old user stack pointer

	/*
	 * We intentionally keep the kernel stack a little below the top of
	 * userspace so we don't have to do a slower byte accurate check here.
	 */
	lw	t5, TI_ADDR_LIMIT($28)
	addu	t4, t0, 32
	and	t5, t4
	bltz	t5, bad_stack		# -> sp is bad

	/*
	 * Ok, copy the args from the luser stack to the kernel stack.
	 */

	.set    push
	.set    noreorder
	.set	nomacro

load_a4: user_lw(t5, 16(t0))		# argument #5 from usp
load_a5: user_lw(t6, 20(t0))		# argument #6 from usp
load_a6: user_lw(t7, 24(t0))		# argument #7 from usp
load_a7: user_lw(t8, 28(t0))		# argument #8 from usp
loads_done:

	sw	t5, 16(sp)		# argument #5 to ksp
	sw	t6, 20(sp)		# argument #6 to ksp
	sw	t7, 24(sp)		# argument #7 to ksp
	sw	t8, 28(sp)		# argument #8 to ksp
	.set	pop

	.section __ex_table,"a"
	PTR	load_a4, bad_stack_a4
	PTR	load_a5, bad_stack_a5
	PTR	load_a6, bad_stack_a6
	PTR	load_a7, bad_stack_a7
	.previous

	lw	t0, TI_FLAGS($28)	# syscall tracing enabled?
	li	t1, _TIF_WORK_SYSCALL_ENTRY
	and	t0, t1
	bnez	t0, syscall_trace_entry # -> yes
syscall_common:
	subu	v0, v0, __NR_O32_Linux	# check syscall number
	sltiu	t0, v0, __NR_O32_Linux_syscalls + 1
	beqz	t0, illegal_syscall

	sll	t0, v0, 2
	la	t1, sys_call_table
	addu	t1, t0
	lw	t2, (t1)		# syscall routine

	beqz	t2, illegal_syscall

	jalr	t2			# Do The Real Thing (TM)

	li	t0, -EMAXERRNO - 1	# error?
	sltu	t0, t0, v0
	sw	t0, PT_R7(sp)		# set error flag
	beqz	t0, 1f

	lw	t1, PT_R2(sp)		# syscall number
	negu	v0			# error
	sw	t1, PT_R0(sp)		# save it for syscall restarting
1:	sw	v0, PT_R2(sp)		# result

o32_syscall_exit:
	j	syscall_exit_partial

/* ------------------------------------------------------------------------ */

syscall_trace_entry:
	SAVE_STATIC
	move	a0, sp

	/*
	 * syscall number is in v0 unless we called syscall(__NR_###)
	 * where the real syscall number is in a0
	 */
	move	a1, v0
	subu	t2, v0,  __NR_O32_Linux
	bnez	t2, 1f /* __NR_syscall at offset 0 */
	lw	a1, PT_R4(sp)

1:	jal	syscall_trace_enter

	bltz	v0, 1f			# seccomp failed? Skip syscall

	RESTORE_STATIC
	lw	v0, PT_R2(sp)		# Restore syscall (maybe modified)
	lw	a0, PT_R4(sp)		# Restore argument registers
	lw	a1, PT_R5(sp)
	lw	a2, PT_R6(sp)
	lw	a3, PT_R7(sp)
	j	syscall_common

1:	j	syscall_exit

/* ------------------------------------------------------------------------ */

	/*
	 * Our open-coded access area sanity test for the stack pointer
	 * failed. We probably should handle this case a bit more drastic.
	 */
bad_stack:
	li	v0, EFAULT
	sw	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sw	t0, PT_R7(sp)
	j	o32_syscall_exit

bad_stack_a4:
	li	t5, 0
	b	load_a5

bad_stack_a5:
	li	t6, 0
	b	load_a6

bad_stack_a6:
	li	t7, 0
	b	load_a7

bad_stack_a7:
	li	t8, 0
	b	loads_done

	/*
	 * The system call does not exist in this kernel
	 */
illegal_syscall:
	li	v0, ENOSYS			# error
	sw	v0, PT_R2(sp)
	li	t0, 1				# set error flag
	sw	t0, PT_R7(sp)
	j	o32_syscall_exit
	END(handle_sys)

	LEAF(sys_syscall)
	subu	t0, a0, __NR_O32_Linux	# check syscall number
	sltiu	v0, t0, __NR_O32_Linux_syscalls + 1
	beqz	t0, einval		# do not recurse
	sll	t1, t0, 2
	beqz	v0, einval
	lw	t2, sys_call_table(t1)		# syscall routine
	sw	a0, PT_R2(sp)			# call routine directly on restart

	/* Some syscalls like execve get their arguments from struct pt_regs
	   and claim zero arguments in the syscall table. Thus we have to
	   assume the worst case and shuffle around all potential arguments.
	   If you want performance, don't use indirect syscalls. */

	move	a0, a1				# shift argument registers
	move	a1, a2
	move	a2, a3
	lw	a3, 16(sp)
	lw	t4, 20(sp)
	lw	t5, 24(sp)
	lw	t6, 28(sp)
	sw	t4, 16(sp)
	sw	t5, 20(sp)
	sw	t6, 24(sp)
	sw	a0, PT_R4(sp)			# .. and push back a0 - a3, some
	sw	a1, PT_R5(sp)			# syscalls expect them there
	sw	a2, PT_R6(sp)
	sw	a3, PT_R7(sp)
	sw	a3, PT_R26(sp)			# update a3 for syscall restarting
	jr	t2
	/* Unreached */

einval: li	v0, -ENOSYS
	jr	ra
	END(sys_syscall)

	.align	2
	.type	sys_call_table, @object
EXPORT(sys_call_table)
	PTR	sys_syscall			/* 4000 */
	PTR	sys_exit
	PTR	__sys_fork
	PTR	sys_read
	PTR	sys_write
	PTR	sys_open			/* 4005 */
	PTR	sys_close
	PTR	sys_waitpid
	PTR	sys_creat
	PTR	sys_link
	PTR	sys_unlink			/* 4010 */
	PTR	sys_execve
	PTR	sys_chdir
	PTR	sys_time
	PTR	sys_mknod
	PTR	sys_chmod			/* 4015 */
	PTR	sys_lchown
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall			/* was sys_stat */
	PTR	sys_lseek
	PTR	sys_getpid			/* 4020 */
	PTR	sys_mount
	PTR	sys_oldumount
	PTR	sys_setuid
	PTR	sys_getuid
	PTR	sys_stime			/* 4025 */
	PTR	sys_ptrace
	PTR	sys_alarm
	PTR	sys_ni_syscall			/* was sys_fstat */
	PTR	sys_pause
	PTR	sys_utime			/* 4030 */
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
	PTR	sys_times
	PTR	sys_ni_syscall
	PTR	sys_brk				/* 4045 */
	PTR	sys_setgid
	PTR	sys_getgid
	PTR	sys_ni_syscall			/* was signal(2) */
	PTR	sys_geteuid
	PTR	sys_getegid			/* 4050 */
	PTR	sys_acct
	PTR	sys_umount
	PTR	sys_ni_syscall
	PTR	sys_ioctl
	PTR	sys_fcntl			/* 4055 */
	PTR	sys_ni_syscall
	PTR	sys_setpgid
	PTR	sys_ni_syscall
	PTR	sys_olduname
	PTR	sys_umask			/* 4060 */
	PTR	sys_chroot
	PTR	sys_ustat
	PTR	sys_dup2
	PTR	sys_getppid
	PTR	sys_getpgrp			/* 4065 */
	PTR	sys_setsid
	PTR	sys_sigaction
	PTR	sys_sgetmask
	PTR	sys_ssetmask
	PTR	sys_setreuid			/* 4070 */
	PTR	sys_setregid
	PTR	sys_sigsuspend
	PTR	sys_sigpending
	PTR	sys_sethostname
	PTR	sys_setrlimit			/* 4075 */
	PTR	sys_getrlimit
	PTR	sys_getrusage
	PTR	sys_gettimeofday
	PTR	sys_settimeofday
	PTR	sys_getgroups			/* 4080 */
	PTR	sys_setgroups
	PTR	sys_ni_syscall			/* old_select */
	PTR	sys_symlink
	PTR	sys_ni_syscall			/* was sys_lstat */
	PTR	sys_readlink			/* 4085 */
	PTR	sys_uselib
	PTR	sys_swapon
	PTR	sys_reboot
	PTR	sys_old_readdir
	PTR	sys_mips_mmap			/* 4090 */
	PTR	sys_munmap
	PTR	sys_truncate
	PTR	sys_ftruncate
	PTR	sys_fchmod
	PTR	sys_fchown			/* 4095 */
	PTR	sys_getpriority
	PTR	sys_setpriority
	PTR	sys_ni_syscall
	PTR	sys_statfs
	PTR	sys_fstatfs			/* 4100 */
	PTR	sys_ni_syscall			/* was ioperm(2) */
	PTR	sys_socketcall
	PTR	sys_syslog
	PTR	sys_setitimer
	PTR	sys_getitimer			/* 4105 */
	PTR	sys_newstat
	PTR	sys_newlstat
	PTR	sys_newfstat
	PTR	sys_uname
	PTR	sys_ni_syscall			/* 4110 was iopl(2) */
	PTR	sys_vhangup
	PTR	sys_ni_syscall			/* was sys_idle() */
	PTR	sys_ni_syscall			/* was sys_vm86 */
	PTR	sys_wait4
	PTR	sys_swapoff			/* 4115 */
	PTR	sys_sysinfo
	PTR	sys_ipc
	PTR	sys_fsync
	PTR	sys_sigreturn
	PTR	__sys_clone			/* 4120 */
	PTR	sys_setdomainname
	PTR	sys_newuname
	PTR	sys_ni_syscall			/* sys_modify_ldt */
	PTR	sys_adjtimex
	PTR	sys_mprotect			/* 4125 */
	PTR	sys_sigprocmask
	PTR	sys_ni_syscall			/* was create_module */
	PTR	sys_init_module
	PTR	sys_delete_module
	PTR	sys_ni_syscall			/* 4130 was get_kernel_syms */
	PTR	sys_quotactl
	PTR	sys_getpgid
	PTR	sys_fchdir
	PTR	sys_bdflush
	PTR	sys_sysfs			/* 4135 */
	PTR	sys_personality
	PTR	sys_ni_syscall			/* for afs_syscall */
	PTR	sys_setfsuid
	PTR	sys_setfsgid
	PTR	sys_llseek			/* 4140 */
	PTR	sys_getdents
	PTR	sys_select
	PTR	sys_flock
	PTR	sys_msync
	PTR	sys_readv			/* 4145 */
	PTR	sys_writev
	PTR	sys_cacheflush
	PTR	sys_cachectl
	PTR	sys_sysmips
	PTR	sys_ni_syscall			/* 4150 */
	PTR	sys_getsid
	PTR	sys_fdatasync
	PTR	sys_sysctl
	PTR	sys_mlock
	PTR	sys_munlock			/* 4155 */
	PTR	sys_mlockall
	PTR	sys_munlockall
	PTR	sys_sched_setparam
	PTR	sys_sched_getparam
	PTR	sys_sched_setscheduler		/* 4160 */
	PTR	sys_sched_getscheduler
	PTR	sys_sched_yield
	PTR	sys_sched_get_priority_max
	PTR	sys_sched_get_priority_min
	PTR	sys_sched_rr_get_interval	/* 4165 */
	PTR	sys_nanosleep
	PTR	sys_mremap
	PTR	sys_accept
	PTR	sys_bind
	PTR	sys_connect			/* 4170 */
	PTR	sys_getpeername
	PTR	sys_getsockname
	PTR	sys_getsockopt
	PTR	sys_listen
	PTR	sys_recv			/* 4175 */
	PTR	sys_recvfrom
	PTR	sys_recvmsg
	PTR	sys_send
	PTR	sys_sendmsg
	PTR	sys_sendto			/* 4180 */
	PTR	sys_setsockopt
	PTR	sys_shutdown
	PTR	sys_socket
	PTR	sys_socketpair
	PTR	sys_setresuid			/* 4185 */
	PTR	sys_getresuid
	PTR	sys_ni_syscall			/* was sys_query_module */
	PTR	sys_poll
	PTR	sys_ni_syscall			/* was nfsservctl */
	PTR	sys_setresgid			/* 4190 */
	PTR	sys_getresgid
	PTR	sys_prctl
	PTR	sys_rt_sigreturn
	PTR	sys_rt_sigaction
	PTR	sys_rt_sigprocmask		/* 4195 */
	PTR	sys_rt_sigpending
	PTR	sys_rt_sigtimedwait
	PTR	sys_rt_sigqueueinfo
	PTR	sys_rt_sigsuspend
	PTR	sys_pread64			/* 4200 */
	PTR	sys_pwrite64
	PTR	sys_chown
	PTR	sys_getcwd
	PTR	sys_capget
	PTR	sys_capset			/* 4205 */
	PTR	sys_sigaltstack
	PTR	sys_sendfile
	PTR	sys_ni_syscall
	PTR	sys_ni_syscall
	PTR	sys_mips_mmap2			/* 4210 */
	PTR	sys_truncate64
	PTR	sys_ftruncate64
	PTR	sys_stat64
	PTR	sys_lstat64
	PTR	sys_fstat64			/* 4215 */
	PTR	sys_pivot_root
	PTR	sys_mincore
	PTR	sys_madvise
	PTR	sys_getdents64
	PTR	sys_fcntl64			/* 4220 */
	PTR	sys_ni_syscall
	PTR	sys_gettid
	PTR	sys_readahead
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
	PTR	sys_futex
#ifdef CONFIG_MIPS_MT_FPAFF
	/*
	 * For FPU affinity scheduling on MIPS MT processors, we need to
	 * intercept sys_sched_xxxaffinity() calls until we get a proper hook
	 * in kernel/sched/core.c.  Considered only temporary we only support
	 * these hooks for the 32-bit kernel - there is no MIPS64 MT processor
	 * atm.
	 */
	PTR	mipsmt_sys_sched_setaffinity
	PTR	mipsmt_sys_sched_getaffinity
#else
	PTR	sys_sched_setaffinity
	PTR	sys_sched_getaffinity		/* 4240 */
#endif /* CONFIG_MIPS_MT_FPAFF */
	PTR	sys_io_setup
	PTR	sys_io_destroy
	PTR	sys_io_getevents
	PTR	sys_io_submit
	PTR	sys_io_cancel			/* 4245 */
	PTR	sys_exit_group
	PTR	sys_lookup_dcookie
	PTR	sys_epoll_create
	PTR	sys_epoll_ctl
	PTR	sys_epoll_wait			/* 4250 */
	PTR	sys_remap_file_pages
	PTR	sys_set_tid_address
	PTR	sys_restart_syscall
	PTR	sys_fadvise64_64
	PTR	sys_statfs64			/* 4255 */
	PTR	sys_fstatfs64
	PTR	sys_timer_create
	PTR	sys_timer_settime
	PTR	sys_timer_gettime
	PTR	sys_timer_getoverrun		/* 4260 */
	PTR	sys_timer_delete
	PTR	sys_clock_settime
	PTR	sys_clock_gettime
	PTR	sys_clock_getres
	PTR	sys_clock_nanosleep		/* 4265 */
	PTR	sys_tgkill
	PTR	sys_utimes
	PTR	sys_mbind
	PTR	sys_get_mempolicy
	PTR	sys_set_mempolicy		/* 4270 */
	PTR	sys_mq_open
	PTR	sys_mq_unlink
	PTR	sys_mq_timedsend
	PTR	sys_mq_timedreceive
	PTR	sys_mq_notify			/* 4275 */
	PTR	sys_mq_getsetattr
	PTR	sys_ni_syscall			/* sys_vserver */
	PTR	sys_waitid
	PTR	sys_ni_syscall			/* available, was setaltroot */
	PTR	sys_add_key			/* 4280 */
	PTR	sys_request_key
	PTR	sys_keyctl
	PTR	sys_set_thread_area
	PTR	sys_inotify_init
	PTR	sys_inotify_add_watch		/* 4285 */
	PTR	sys_inotify_rm_watch
	PTR	sys_migrate_pages
	PTR	sys_openat
	PTR	sys_mkdirat
	PTR	sys_mknodat			/* 4290 */
	PTR	sys_fchownat
	PTR	sys_futimesat
	PTR	sys_fstatat64
	PTR	sys_unlinkat
	PTR	sys_renameat			/* 4295 */
	PTR	sys_linkat
	PTR	sys_symlinkat
	PTR	sys_readlinkat
	PTR	sys_fchmodat
	PTR	sys_faccessat			/* 4300 */
	PTR	sys_pselect6
	PTR	sys_ppoll
	PTR	sys_unshare
	PTR	sys_splice
	PTR	sys_sync_file_range		/* 4305 */
	PTR	sys_tee
	PTR	sys_vmsplice
	PTR	sys_move_pages
	PTR	sys_set_robust_list
	PTR	sys_get_robust_list		/* 4310 */
	PTR	sys_kexec_load
	PTR	sys_getcpu
	PTR	sys_epoll_pwait
	PTR	sys_ioprio_set
	PTR	sys_ioprio_get			/* 4315 */
	PTR	sys_utimensat
	PTR	sys_signalfd
	PTR	sys_ni_syscall			/* was timerfd */
	PTR	sys_eventfd
	PTR	sys_fallocate			/* 4320 */
	PTR	sys_timerfd_create
	PTR	sys_timerfd_gettime
	PTR	sys_timerfd_settime
	PTR	sys_signalfd4
	PTR	sys_eventfd2			/* 4325 */
	PTR	sys_epoll_create1
	PTR	sys_dup3
	PTR	sys_pipe2
	PTR	sys_inotify_init1
	PTR	sys_preadv			/* 4330 */
	PTR	sys_pwritev
	PTR	sys_rt_tgsigqueueinfo
	PTR	sys_perf_event_open
	PTR	sys_accept4
	PTR	sys_recvmmsg			/* 4335 */
	PTR	sys_fanotify_init
	PTR	sys_fanotify_mark
	PTR	sys_prlimit64
	PTR	sys_name_to_handle_at
	PTR	sys_open_by_handle_at		/* 4340 */
	PTR	sys_clock_adjtime
	PTR	sys_syncfs
	PTR	sys_sendmmsg
	PTR	sys_setns
	PTR	sys_process_vm_readv		/* 4345 */
	PTR	sys_process_vm_writev
	PTR	sys_kcmp
	PTR	sys_finit_module
	PTR	sys_sched_setattr
	PTR	sys_sched_getattr		/* 4350 */
	PTR	sys_renameat2
	PTR	sys_seccomp
	PTR	sys_getrandom
	PTR	sys_memfd_create
	PTR	sys_bpf				/* 4355 */
	PTR	sys_execveat
	PTR	sys_userfaultfd
	PTR	sys_membarrier
	PTR	sys_mlock2
	PTR	sys_copy_file_range		/* 4360 */
	PTR	sys_preadv2
	PTR	sys_pwritev2
	PTR	sys_pkey_mprotect
	PTR	sys_pkey_alloc
	PTR	sys_pkey_free			/* 4365 */
