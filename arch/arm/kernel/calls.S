/*
 *  linux/arch/arm/kernel/calls.S
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file is included twice in entry-common.S
 */
#ifndef NR_syscalls
#define NR_syscalls 328
#else

100:
/* 0 */		.long	sys_restart_syscall
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
		.long	OBSOLETE(sys_time)	/* used by libc4 */
		.long	sys_mknod
/* 15 */	.long	sys_chmod
		.long	sys_lchown16
		.long	sys_ni_syscall		/* was sys_break */
		.long	sys_ni_syscall		/* was sys_stat */
		.long	sys_lseek
/* 20 */	.long	sys_getpid
		.long	sys_mount
		.long	OBSOLETE(sys_oldumount)	/* used by libc4 */
		.long	sys_setuid16
		.long	sys_getuid16
/* 25 */	.long	OBSOLETE(sys_stime)
		.long	sys_ptrace
		.long	OBSOLETE(sys_alarm)	/* used by libc4 */
		.long	sys_ni_syscall		/* was sys_fstat */
		.long	sys_pause
/* 30 */	.long	OBSOLETE(sys_utime)	/* used by libc4 */
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
		.long	OBSOLETE(sys_old_getrlimit) /* used by libc4 */
		.long	sys_getrusage
		.long	sys_gettimeofday
		.long	sys_settimeofday
/* 80 */	.long	sys_getgroups16
		.long	sys_setgroups16
		.long	OBSOLETE(old_select)	/* used by libc4 */
		.long	sys_symlink
		.long	sys_ni_syscall		/* was sys_lstat */
/* 85 */	.long	sys_readlink
		.long	sys_uselib
		.long	sys_swapon
		.long	sys_reboot
		.long	OBSOLETE(old_readdir)	/* used by libc4 */
/* 90 */	.long	OBSOLETE(old_mmap)	/* used by libc4 */
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
		.long	OBSOLETE(sys_socketcall)
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
		.long	OBSOLETE(sys_syscall)	/* call a syscall */
		.long	sys_wait4
/* 115 */	.long	sys_swapoff
		.long	sys_sysinfo
		.long	OBSOLETE(ABI(sys_ipc, sys_oabi_ipc))
		.long	sys_fsync
		.long	sys_sigreturn_wrapper
/* 120 */	.long	sys_clone_wrapper
		.long	sys_setdomainname
		.long	sys_newuname
		.long	sys_ni_syscall
		.long	sys_adjtimex
/* 125 */	.long	sys_mprotect
		.long	sys_sigprocmask
		.long	sys_ni_syscall		/* was sys_create_module */
		.long	sys_init_module
		.long	sys_delete_module
/* 130 */	.long	sys_ni_syscall		/* was sys_get_kernel_syms */
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
		.long	sys_ni_syscall		/* was sys_query_module */
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
/* 180 */	.long	ABI(sys_pread64, sys_oabi_pread64)
		.long	ABI(sys_pwrite64, sys_oabi_pwrite64)
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
		.long	ABI(sys_truncate64, sys_oabi_truncate64)
		.long	ABI(sys_ftruncate64, sys_oabi_ftruncate64)
/* 195 */	.long	ABI(sys_stat64, sys_oabi_stat64)
		.long	ABI(sys_lstat64, sys_oabi_lstat64)
		.long	ABI(sys_fstat64, sys_oabi_fstat64)
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
		.long	ABI(sys_fcntl64, sys_oabi_fcntl64)
		.long	sys_ni_syscall /* TUX */
		.long	sys_ni_syscall
		.long	sys_gettid
/* 225 */	.long	ABI(sys_readahead, sys_oabi_readahead)
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
		.long	sys_sendfile64
/* 240 */	.long	sys_futex
		.long	sys_sched_setaffinity
		.long	sys_sched_getaffinity
		.long	sys_io_setup
		.long	sys_io_destroy
/* 245 */	.long	sys_io_getevents
		.long	sys_io_submit
		.long	sys_io_cancel
		.long	sys_exit_group
		.long	sys_lookup_dcookie
/* 250 */	.long	sys_epoll_create
		.long	ABI(sys_epoll_ctl, sys_oabi_epoll_ctl)
		.long	ABI(sys_epoll_wait, sys_oabi_epoll_wait)
	 	.long	sys_remap_file_pages
		.long	sys_ni_syscall	/* sys_set_thread_area */
/* 255 */	.long	sys_ni_syscall	/* sys_get_thread_area */
 		.long	sys_set_tid_address
		.long	sys_timer_create
		.long	sys_timer_settime
		.long	sys_timer_gettime
/* 260 */	.long	sys_timer_getoverrun
		.long	sys_timer_delete
		.long	sys_clock_settime
		.long	sys_clock_gettime
		.long	sys_clock_getres
/* 265 */	.long	sys_clock_nanosleep
		.long	sys_statfs64_wrapper
		.long	sys_fstatfs64_wrapper
		.long	sys_tgkill
		.long	sys_utimes
/* 270 */	.long	sys_arm_fadvise64_64
		.long	sys_pciconfig_iobase
		.long	sys_pciconfig_read
		.long	sys_pciconfig_write
		.long	sys_mq_open
/* 275 */	.long	sys_mq_unlink
		.long	sys_mq_timedsend
		.long	sys_mq_timedreceive
		.long	sys_mq_notify
		.long	sys_mq_getsetattr
/* 280 */	.long	sys_waitid
		.long	sys_socket
		.long	sys_bind
		.long	sys_connect
		.long	sys_listen
/* 285 */	.long	sys_accept
		.long	sys_getsockname
		.long	sys_getpeername
		.long	sys_socketpair
		.long	sys_send
/* 290 */	.long	sys_sendto
		.long	sys_recv
		.long	sys_recvfrom
		.long	sys_shutdown
		.long	sys_setsockopt
/* 295 */	.long	sys_getsockopt
		.long	sys_sendmsg
		.long	sys_recvmsg
		.long	ABI(sys_semop, sys_oabi_semop)
		.long	sys_semget
/* 300 */	.long	sys_semctl
		.long	sys_msgsnd
		.long	sys_msgrcv
		.long	sys_msgget
		.long	sys_msgctl
/* 305 */	.long	sys_shmat
		.long	sys_shmdt
		.long	sys_shmget
		.long	sys_shmctl
		.long	sys_add_key
/* 310 */	.long	sys_request_key
		.long	sys_keyctl
		.long	ABI(sys_semtimedop, sys_oabi_semtimedop)
/* vserver */	.long	sys_ni_syscall
		.long	sys_ioprio_set
/* 315 */	.long	sys_ioprio_get
		.long	sys_inotify_init
		.long	sys_inotify_add_watch
		.long	sys_inotify_rm_watch
		.long	sys_mbind
/* 320 */	.long	sys_get_mempolicy
		.long	sys_set_mempolicy

		.rept	NR_syscalls - (. - 100b) / 4
			.long	sys_ni_syscall
		.endr
#endif
