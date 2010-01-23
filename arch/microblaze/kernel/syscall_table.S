ENTRY(sys_call_table)
	.long sys_restart_syscall	/* 0 - old "setup()" system call,
					 * used for restarting */
	.long sys_exit
#ifdef CONFIG_MMU
	.long sys_fork_wrapper
#else
	.long sys_ni_syscall
#endif
	.long sys_read
	.long sys_write
	.long sys_open			/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink		/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod			/* 15 */
	.long sys_lchown
	.long sys_ni_syscall		/* old break syscall holder */
	.long sys_ni_syscall		/* old stat */
	.long sys_lseek
	.long sys_getpid		/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime			/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_ni_syscall		/* oldfstat */
	.long sys_pause
	.long sys_utime			/* 30 */
	.long sys_ni_syscall		/* old stty syscall holder */
	.long sys_ni_syscall		/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall		/* 35 - old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir			/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall		/* old prof syscall holder */
	.long sys_brk			/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid		/* 50 */
	.long sys_acct
	.long sys_umount		/* recycled never used phys() */
	.long sys_ni_syscall		/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl			/* 55 */
	.long sys_ni_syscall		/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall		/* old ulimit syscall holder */
	.long sys_ni_syscall		/* olduname */
	.long sys_umask			/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp		/* 65 */
	.long sys_setsid
	.long sys_ni_syscall		/* sys_sigaction */
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid		/* 70 */
	.long sys_setregid
	.long sys_ni_syscall		/* sys_sigsuspend_wrapper */
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit		/* 75 */
	.long sys_ni_syscall		/* old_getrlimit */
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups		/* 80 */
	.long sys_setgroups
	.long sys_ni_syscall		/* old_select */
	.long sys_symlink
	.long sys_ni_syscall		/* oldlstat */
	.long sys_readlink		/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_ni_syscall		/* old_readdir */
	.long sys_mmap			/* 90 */ /* old_mmap */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown		/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall		/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs		/* 100 */
	.long sys_ni_syscall		/* ioperm */
	.long sys_socketcall
	.long sys_syslog		/* operation with system console */
	.long sys_setitimer
	.long sys_getitimer		/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall		/* uname */
	.long sys_ni_syscall		/* 110 */ /* iopl */
	.long sys_vhangup
	.long sys_ni_syscall		/* old "idle" system call */
	.long sys_ni_syscall		/* old sys_vm86old */
	.long sys_wait4
	.long sys_swapoff		/* 115 */
	.long sys_sysinfo
	.long sys_ni_syscall		/* old sys_ipc */
	.long sys_fsync
	.long sys_ni_syscall		/* sys_sigreturn_wrapper */
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall		/* modify_ldt */
	.long sys_adjtimex
	.long sys_mprotect		/* 125:	sys_mprotect */
	.long sys_sigprocmask
	.long sys_ni_syscall		/* old "create_module" */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall		/* 130:	old "get_kernel_syms" */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs			/* 135 */
	.long sys_personality
	.long sys_ni_syscall		/* reserved for afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek		/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv			/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150:	sys_mlock */
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
	.long sys_setresuid
	.long sys_getresuid		/* 165 */
	.long sys_ni_syscall		/* sys_vm86 */
	.long sys_ni_syscall		/* Old sys_query_module */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid		/* 170 */
	.long sys_getresgid
	.long sys_prctl
	.long sys_rt_sigreturn_wrapper
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64		/* 180 */
	.long sys_pwrite64
	.long sys_chown
	.long sys_getcwd
	.long sys_capget
	.long sys_capset		/* 185 */
	.long sys_ni_syscall		/* sigaltstack */
	.long sys_sendfile
	.long sys_ni_syscall		/* reserved for streams1 */
	.long sys_ni_syscall		/* reserved for streams2 */
	.long sys_vfork		/* 190 */
	.long sys_getrlimit
	.long sys_mmap_pgoff		/* mmap2 */
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64		/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_lchown
	.long sys_getuid
	.long sys_getgid		/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups		/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid		/* 210 */
	.long sys_getresgid
	.long sys_chown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid		/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_mincore
	.long sys_madvise
	.long sys_getdents64		/* 220 */
	.long sys_fcntl64
	.long sys_ni_syscall		/* reserved for TUX */
	.long sys_ni_syscall
	.long sys_gettid
	.long sys_readahead		/* 225 */
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr
	.long sys_getxattr
	.long sys_lgetxattr		/* 230 */
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr		/* 235 */
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_tkill
	.long sys_sendfile64
	.long sys_futex			/* 240 */
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_ni_syscall		/* set_thread_area */
	.long sys_ni_syscall		/* get_thread_area */
	.long sys_io_setup		/* 245 */
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit
	.long sys_io_cancel
	.long sys_fadvise64		/* 250 */
	.long sys_ni_syscall
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl		/* 255 */
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
	.long sys_ni_syscall		/* sys_vserver */
	.long sys_mbind
	.long sys_get_mempolicy
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
	.long sys_ioprio_set
	.long sys_ioprio_get		/* 290 */
	.long sys_inotify_init
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
	.long sys_ni_syscall		/* sys_migrate_pages */
	.long sys_openat	/* 295 */
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
	.long sys_ni_syscall /* pselect6 */
	.long sys_ppoll
	.long sys_unshare		/* 310 */
	.long sys_set_robust_list
	.long sys_get_robust_list
	.long sys_splice
	.long sys_sync_file_range
	.long sys_tee 			/* 315 */
	.long sys_vmsplice
	.long sys_move_pages
	.long sys_getcpu
	.long sys_epoll_pwait
	.long sys_utimensat		/* 320 */
	.long sys_signalfd
	.long sys_timerfd_create
	.long sys_eventfd
	.long sys_fallocate
	.long sys_semtimedop		/* 325 */
	.long sys_timerfd_settime
	.long sys_timerfd_gettime
	.long sys_semctl
	.long sys_semget
	.long sys_semop			/* 330 */
	.long sys_msgctl
	.long sys_msgget
	.long sys_msgrcv
	.long sys_msgsnd
	.long sys_shmat			/* 335 */
	.long sys_shmctl
	.long sys_shmdt
	.long sys_shmget
	.long sys_signalfd4	/* new syscall */
	.long sys_eventfd2		/* 340 */
	.long sys_epoll_create1
	.long sys_dup3
	.long sys_pipe2
	.long sys_inotify_init1
	.long sys_socket		/* 345 */
	.long sys_socketpair
	.long sys_bind
	.long sys_listen
	.long sys_accept
	.long sys_connect		/* 350 */
	.long sys_getsockname
	.long sys_getpeername
	.long sys_sendto
	.long sys_send
	.long sys_recvfrom		/* 355 */
	.long sys_recv
	.long sys_setsockopt
	.long sys_getsockopt
	.long sys_shutdown
	.long sys_sendmsg		/* 360 */
	.long sys_recvmsg
	.long sys_accept4
	.long sys_ni_syscall
	.long sys_ni_syscall
	.long sys_rt_tgsigqueueinfo	/* 365 */
	.long sys_perf_event_open
	.long sys_recvmmsg
