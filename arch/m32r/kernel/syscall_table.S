ENTRY(sys_call_table)
	.long sys_restart_syscall	/* 0  -  old "setup()" system call*/
	.long sys_exit
	.long sys_fork
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
	.long sys_ni_syscall		/* lchown16 syscall holder */
	.long sys_ni_syscall		/* old break syscall holder */
	.long sys_ni_syscall		/* old stat syscall holder */
	.long sys_lseek
	.long sys_getpid		/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_ni_syscall		/* setuid16 syscall holder */
	.long sys_ni_syscall		/* getuid16 syscall holder */
	.long sys_stime			/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_ni_syscall		/* old fstat syscall holder */
	.long sys_pause
	.long sys_utime			/* 30 */
	.long sys_ni_syscall		/* old stty syscall holder */
	.long sys_cachectl		/* for M32R */ /* old gtty syscall holder */
	.long sys_access
	.long sys_ni_syscall		/* nice	syscall holder */
	.long sys_ni_syscall		/* 35  -  old ftime syscall holder */
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
	.long sys_ni_syscall		/* setgid16 syscall holder */
	.long sys_getgid		/* will be unused */
	.long sys_ni_syscall		/* signal syscall holder */
	.long sys_ni_syscall		/* geteuid16  syscall holder */
	.long sys_ni_syscall		/* 50 - getegid16 syscall holder */
	.long sys_acct
	.long sys_umount		/* recycled never used phys() */
	.long sys_ni_syscall		/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl			/* 55 - will be unused */
	.long sys_ni_syscall		/* mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall		/* old ulimit syscall holder */
	.long sys_ni_syscall		/* sys_olduname */
	.long sys_umask			/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp		/* 65 */
	.long sys_setsid
	.long sys_ni_syscall		/* sigaction syscall holder */
	.long sys_ni_syscall		/* sgetmask syscall holder */
	.long sys_ni_syscall		/* ssetmask syscall holder */
	.long sys_ni_syscall		/* 70 - setreuid16 syscall holder */
	.long sys_ni_syscall		/* setregid16 syscall holder */
	.long sys_ni_syscall		/* sigsuspend syscall holder */
	.long sys_ni_syscall		/* sigpending syscall holder */
	.long sys_sethostname
	.long sys_setrlimit		/* 75 */
	.long sys_getrlimit/*will be unused*/
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_ni_syscall		/* 80 - getgroups16 syscall holder */
	.long sys_ni_syscall		/* setgroups16 syscall holder */
	.long sys_ni_syscall		/* sys_oldselect */
	.long sys_symlink
	.long sys_ni_syscall		/* old lstat syscall holder */
	.long sys_readlink		/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_ni_syscall		/* readdir syscall holder */
	.long sys_ni_syscall		/* 90 - old_mmap syscall holder */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_ni_syscall		/* 95 - fchwon16  syscall holder */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall		/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs		/* 100 */
	.long sys_ni_syscall		/* ioperm syscall holder */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer		/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall		/* old uname syscall holder */
	.long sys_ni_syscall		/* 110  -  iopl syscall holder */
	.long sys_vhangup
	.long sys_ni_syscall		/* idle syscall holder */
	.long sys_ni_syscall		/* vm86old syscall holder */
	.long sys_wait4
	.long sys_swapoff		/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_ni_syscall		/* sigreturn syscall holder */
	.long sys_clone			/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall		/* modify_ldt syscall holder */
	.long sys_adjtimex
	.long sys_mprotect		/* 125 */
	.long sys_ni_syscall		/* sigprocmask syscall holder */
	.long sys_ni_syscall		/* create_module syscall holder */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall		/* 130 - get_kernel_syms */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs			/* 135 */
	.long sys_personality
	.long sys_ni_syscall		/* afs_syscall syscall holder */
	.long sys_ni_syscall		/* setfsuid16 syscall holder */
	.long sys_ni_syscall		/* setfsgid16 syscall holder */
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
	.long sys_mlock			/* 150 */
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
	.long sys_ni_syscall		/* setresuid16 syscall holder */
	.long sys_ni_syscall		/* 165 - getresuid16 syscall holder */
	.long sys_tas			/* vm86 syscall holder */
	.long sys_ni_syscall		/* query_module syscall holder */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid		/* 170 */
	.long sys_getresgid
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64		/* 180 */
	.long sys_pwrite64
	.long sys_ni_syscall		/* chown16 syscall holder */
	.long sys_getcwd
	.long sys_capget
	.long sys_capset		/* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long sys_vfork			/* 190 */
	.long sys_getrlimit
	.long sys_mmap2
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
	.long sys_ni_syscall		/* Reserved for Security */
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
	.long sys_ni_syscall		/* reserved for "set_thread_area" system call */
	.long sys_ni_syscall		/* reserved for "get_thread_area" system call */
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
	.long sys_ni_syscall		/* Reserved for sys_vserver */
        .long sys_ni_syscall		/* Reserved for sys_mbind */
        .long sys_ni_syscall		/* Reserved for sys_get_mempolicy */
        .long sys_ni_syscall		/* Reserved for sys_set_mempolicy */
        .long sys_mq_open
        .long sys_mq_unlink
        .long sys_mq_timedsend
        .long sys_mq_timedreceive       /* 280 */
        .long sys_mq_notify
        .long sys_mq_getsetattr
        .long sys_ni_syscall            /* reserved for kexec */
	.long sys_waitid
