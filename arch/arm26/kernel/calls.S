/*
 *  linux/arch/arm26/kernel/calls.S
 *
 *  Copyright (C) 2003 Ian Molton
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  FIXME
 *  This file is included twice in entry.S which may not be necessary
 */

//FIXME - clearly NR_syscalls is never defined here

#ifndef NR_syscalls
#define NR_syscalls 256
#else

__syscall_start:
/* 0 */		.long	sys_ni_syscall
		.long	sys_exit
		.long	sys_fork_wrapper
		.long	sys_read
		.long	sys_write
/* 5 */		.long	sys_open
		.long	sys_close
		.long	sys_ni_syscall		/* was sys_waitpid */
		.long	sys_creat
		.long	sys_link
/* 10 */	.long	sys_unlink
		.long	sys_execve_wrapper
		.long	sys_chdir
		.long	sys_time		/* used by libc4 */
		.long	sys_mknod
/* 15 */	.long	sys_chmod
		.long	sys_lchown16
		.long	sys_ni_syscall		/* was sys_break */
		.long	sys_ni_syscall		/* was sys_stat */
		.long	sys_lseek
/* 20 */	.long	sys_getpid
		.long	sys_mount
		.long	sys_oldumount		/* used by libc4 */
		.long	sys_setuid16
		.long	sys_getuid16
/* 25 */	.long	sys_stime
		.long	sys_ptrace
		.long	sys_alarm		/* used by libc4 */
		.long	sys_ni_syscall		/* was sys_fstat */
		.long	sys_pause
/* 30 */	.long	sys_utime		/* used by libc4 */
		.long	sys_ni_syscall		/* was sys_stty */
		.long	sys_ni_syscall		/* was sys_getty */
		.long	sys_access
		.long	sys_nice
/* 35 */	.long	sys_ni_syscall		/* was sys_ftime */
		.long	sys_sync
		.long	sys_kill
		.long	sys_rename
		.long	sys_mkdir
/* 40 */	.long	sys_rmdir
		.long	sys_dup
		.long	sys_pipe
		.long	sys_times
		.long	sys_ni_syscall		/* was sys_prof */
/* 45 */	.long	sys_brk
		.long	sys_setgid16
		.long	sys_getgid16
		.long	sys_ni_syscall		/* was sys_signal */
		.long	sys_geteuid16
/* 50 */	.long	sys_getegid16
		.long	sys_acct
		.long	sys_umount
		.long	sys_ni_syscall		/* was sys_lock */
		.long	sys_ioctl
/* 55 */	.long	sys_fcntl
		.long	sys_ni_syscall		/* was sys_mpx */
		.long	sys_setpgid
		.long	sys_ni_syscall		/* was sys_ulimit */
		.long	sys_ni_syscall		/* was sys_olduname */
/* 60 */	.long	sys_umask
		.long	sys_chroot
		.long	sys_ustat
		.long	sys_dup2
		.long	sys_getppid
/* 65 */	.long	sys_getpgrp
		.long	sys_setsid
		.long	sys_sigaction
		.long	sys_ni_syscall		/* was sys_sgetmask */
		.long	sys_ni_syscall		/* was sys_ssetmask */
/* 70 */	.long	sys_setreuid16
		.long	sys_setregid16
		.long	sys_sigsuspend_wrapper
		.long	sys_sigpending
		.long	sys_sethostname
/* 75 */	.long	sys_setrlimit
		.long	sys_old_getrlimit	/* used by libc4 */
		.long	sys_getrusage
		.long	sys_gettimeofday
		.long	sys_settimeofday
/* 80 */	.long	sys_getgroups16
		.long	sys_setgroups16
		.long	old_select		/* used by libc4 */
		.long	sys_symlink
		.long	sys_ni_syscall		/* was sys_lstat */
/* 85 */	.long	sys_readlink
		.long	sys_uselib
		.long	sys_swapon
		.long	sys_reboot
		.long	old_readdir		/* used by libc4 */
/* 90 */	.long	old_mmap		/* used by libc4 */
		.long	sys_munmap
		.long	sys_truncate
		.long	sys_ftruncate
		.long	sys_fchmod
/* 95 */	.long	sys_fchown16
		.long	sys_getpriority
		.long	sys_setpriority
		.long	sys_ni_syscall		/* was sys_profil */
		.long	sys_statfs
/* 100 */	.long	sys_fstatfs
		.long	sys_ni_syscall
		.long	sys_socketcall
		.long	sys_syslog
		.long	sys_setitimer
/* 105 */	.long	sys_getitimer
		.long	sys_newstat
		.long	sys_newlstat
		.long	sys_newfstat
		.long	sys_ni_syscall		/* was sys_uname */
/* 110 */	.long	sys_ni_syscall		/* was sys_iopl */
		.long	sys_vhangup
		.long	sys_ni_syscall
		.long	sys_syscall		/* call a syscall */
		.long	sys_wait4
/* 115 */	.long	sys_swapoff
		.long	sys_sysinfo
		.long	sys_ipc
		.long	sys_fsync
		.long	sys_sigreturn_wrapper
/* 120 */	.long	sys_clone_wapper
		.long	sys_setdomainname
		.long	sys_newuname
		.long	sys_ni_syscall
		.long	sys_adjtimex
/* 125 */	.long	sys_mprotect
		.long	sys_sigprocmask
		.long	sys_ni_syscall  /* WAS: sys_create_module */
		.long	sys_init_module
		.long	sys_delete_module
/* 130 */	.long	sys_ni_syscall  /* WAS: sys_get_kernel_syms */
		.long	sys_quotactl
		.long	sys_getpgid
		.long	sys_fchdir
		.long	sys_bdflush
/* 135 */	.long	sys_sysfs
		.long	sys_personality
		.long	sys_ni_syscall		/* .long	_sys_afs_syscall */
		.long	sys_setfsuid16
		.long	sys_setfsgid16
/* 140 */	.long	sys_llseek
		.long	sys_getdents
		.long	sys_select
		.long	sys_flock
		.long	sys_msync
/* 145 */	.long	sys_readv
		.long	sys_writev
		.long	sys_getsid
		.long	sys_fdatasync
		.long	sys_sysctl
/* 150 */	.long	sys_mlock
		.long	sys_munlock
		.long	sys_mlockall
		.long	sys_munlockall
		.long	sys_sched_setparam
/* 155 */	.long	sys_sched_getparam
		.long	sys_sched_setscheduler
		.long	sys_sched_getscheduler
		.long	sys_sched_yield
		.long	sys_sched_get_priority_max
/* 160 */	.long	sys_sched_get_priority_min
		.long	sys_sched_rr_get_interval
		.long	sys_nanosleep
		.long	sys_arm_mremap
		.long	sys_setresuid16
/* 165 */	.long	sys_getresuid16
		.long	sys_ni_syscall
		.long	sys_ni_syscall /* WAS: sys_query_module */
		.long	sys_poll
		.long	sys_nfsservctl
/* 170 */	.long	sys_setresgid16
		.long	sys_getresgid16
		.long	sys_prctl
		.long	sys_rt_sigreturn_wrapper
		.long	sys_rt_sigaction
/* 175 */	.long	sys_rt_sigprocmask
		.long	sys_rt_sigpending
		.long	sys_rt_sigtimedwait
		.long	sys_rt_sigqueueinfo
		.long	sys_rt_sigsuspend_wrapper
/* 180 */	.long	sys_pread64
		.long	sys_pwrite64
		.long	sys_chown16
		.long	sys_getcwd
		.long	sys_capget
/* 185 */	.long	sys_capset
		.long	sys_sigaltstack_wrapper
		.long	sys_sendfile
		.long	sys_ni_syscall
		.long	sys_ni_syscall
/* 190 */	.long	sys_vfork_wrapper
		.long	sys_getrlimit
		.long	sys_mmap2
		.long	sys_truncate64
		.long	sys_ftruncate64
/* 195 */	.long	sys_stat64
		.long	sys_lstat64
		.long	sys_fstat64
		.long	sys_lchown
		.long	sys_getuid
/* 200 */	.long	sys_getgid
		.long	sys_geteuid
		.long	sys_getegid
		.long	sys_setreuid
		.long	sys_setregid
/* 205 */	.long	sys_getgroups
		.long	sys_setgroups
		.long	sys_fchown
		.long	sys_setresuid
		.long	sys_getresuid
/* 210 */	.long	sys_setresgid
		.long	sys_getresgid
		.long	sys_chown
		.long	sys_setuid
		.long	sys_setgid
/* 215 */	.long	sys_setfsuid
		.long	sys_setfsgid
		.long	sys_getdents64
		.long	sys_pivot_root
		.long	sys_mincore
/* 220 */	.long	sys_madvise
		.long	sys_fcntl64
		.long	sys_ni_syscall /* TUX */
		.long	sys_ni_syscall /* WAS: sys_security */
		.long	sys_gettid
/* 225 */	.long	sys_readahead
		.long	sys_setxattr
		.long	sys_lsetxattr
		.long	sys_fsetxattr
		.long	sys_getxattr
/* 230 */	.long	sys_lgetxattr
		.long	sys_fgetxattr
		.long	sys_listxattr
		.long	sys_llistxattr
		.long	sys_flistxattr
/* 235 */	.long	sys_removexattr
		.long	sys_lremovexattr
		.long	sys_fremovexattr
		.long	sys_tkill
__syscall_end:

		.rept	NR_syscalls - (__syscall_end - __syscall_start) / 4
			.long	sys_ni_syscall
		.endr
#endif
