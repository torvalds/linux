/* systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995, 2007 David S. Miller (davem@davemloft.net)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */


	.data
	.align 4

	/* First, the Linux native syscall table. */

	.globl sys_call_table
sys_call_table:
/*0*/	.long sys_restart_syscall, sys_exit, sys_fork, sys_read, sys_write
/*5*/	.long sys_open, sys_close, sys_wait4, sys_creat, sys_link
/*10*/  .long sys_unlink, sunos_execv, sys_chdir, sys_chown16, sys_mknod
/*15*/	.long sys_chmod, sys_lchown16, sys_brk, sys_nis_syscall, sys_lseek
/*20*/	.long sys_getpid, sys_capget, sys_capset, sys_setuid16, sys_getuid16
/*25*/	.long sys_vmsplice, sys_ptrace, sys_alarm, sys_sigaltstack, sys_pause
/*30*/	.long sys_utime, sys_lchown, sys_fchown, sys_access, sys_nice
/*35*/	.long sys_chown, sys_sync, sys_kill, sys_newstat, sys_sendfile
/*40*/	.long sys_newlstat, sys_dup, sys_sparc_pipe, sys_times, sys_getuid
/*45*/	.long sys_umount, sys_setgid16, sys_getgid16, sys_signal, sys_geteuid16
/*50*/	.long sys_getegid16, sys_acct, sys_nis_syscall, sys_getgid, sys_ioctl
/*55*/	.long sys_reboot, sys_mmap2, sys_symlink, sys_readlink, sys_execve
/*60*/	.long sys_umask, sys_chroot, sys_newfstat, sys_fstat64, sys_getpagesize
/*65*/	.long sys_msync, sys_vfork, sys_pread64, sys_pwrite64, sys_geteuid
/*70*/	.long sys_getegid, sys_mmap, sys_setreuid, sys_munmap, sys_mprotect
/*75*/	.long sys_madvise, sys_vhangup, sys_truncate64, sys_mincore, sys_getgroups16
/*80*/	.long sys_setgroups16, sys_getpgrp, sys_setgroups, sys_setitimer, sys_ftruncate64
/*85*/	.long sys_swapon, sys_getitimer, sys_setuid, sys_sethostname, sys_setgid
/*90*/	.long sys_dup2, sys_setfsuid, sys_fcntl, sys_select, sys_setfsgid
/*95*/	.long sys_fsync, sys_setpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*100*/	.long sys_getpriority, sys_rt_sigreturn, sys_rt_sigaction, sys_rt_sigprocmask, sys_rt_sigpending
/*105*/	.long sys_rt_sigtimedwait, sys_rt_sigqueueinfo, sys_rt_sigsuspend, sys_setresuid, sys_getresuid
/*110*/	.long sys_setresgid, sys_getresgid, sys_setregid, sys_nis_syscall, sys_nis_syscall
/*115*/	.long sys_getgroups, sys_gettimeofday, sys_getrusage, sys_nis_syscall, sys_getcwd
/*120*/	.long sys_readv, sys_writev, sys_settimeofday, sys_fchown16, sys_fchmod
/*125*/	.long sys_nis_syscall, sys_setreuid16, sys_setregid16, sys_rename, sys_truncate
/*130*/	.long sys_ftruncate, sys_flock, sys_lstat64, sys_nis_syscall, sys_nis_syscall
/*135*/	.long sys_nis_syscall, sys_mkdir, sys_rmdir, sys_utimes, sys_stat64
/*140*/	.long sys_sendfile64, sys_nis_syscall, sys_futex, sys_gettid, sys_getrlimit
/*145*/	.long sys_setrlimit, sys_pivot_root, sys_prctl, sys_pciconfig_read, sys_pciconfig_write
/*150*/	.long sys_nis_syscall, sys_inotify_init, sys_inotify_add_watch, sys_poll, sys_getdents64
/*155*/	.long sys_fcntl64, sys_inotify_rm_watch, sys_statfs, sys_fstatfs, sys_oldumount
/*160*/	.long sys_sched_setaffinity, sys_sched_getaffinity, sys_getdomainname, sys_setdomainname, sys_nis_syscall
/*165*/	.long sys_quotactl, sys_set_tid_address, sys_mount, sys_ustat, sys_setxattr
/*170*/	.long sys_lsetxattr, sys_fsetxattr, sys_getxattr, sys_lgetxattr, sys_getdents
/*175*/	.long sys_setsid, sys_fchdir, sys_fgetxattr, sys_listxattr, sys_llistxattr
/*180*/	.long sys_flistxattr, sys_removexattr, sys_lremovexattr, sys_sigpending, sys_ni_syscall
/*185*/	.long sys_setpgid, sys_fremovexattr, sys_tkill, sys_exit_group, sys_newuname
/*190*/	.long sys_init_module, sys_personality, sparc_remap_file_pages, sys_epoll_create, sys_epoll_ctl
/*195*/	.long sys_epoll_wait, sys_ioprio_set, sys_getppid, sparc_sigaction, sys_sgetmask
/*200*/	.long sys_ssetmask, sys_sigsuspend, sys_newlstat, sys_uselib, sys_old_readdir
/*205*/	.long sys_readahead, sys_socketcall, sys_syslog, sys_lookup_dcookie, sys_fadvise64
/*210*/	.long sys_fadvise64_64, sys_tgkill, sys_waitpid, sys_swapoff, sys_sysinfo
/*215*/	.long sys_ipc, sys_sigreturn, sys_clone, sys_ioprio_get, sys_adjtimex
/*220*/	.long sys_sigprocmask, sys_ni_syscall, sys_delete_module, sys_ni_syscall, sys_getpgid
/*225*/	.long sys_bdflush, sys_sysfs, sys_nis_syscall, sys_setfsuid16, sys_setfsgid16
/*230*/	.long sys_select, sys_time, sys_splice, sys_stime, sys_statfs64
					  /* "We are the Knights of the Forest of Ni!!" */
/*235*/	.long sys_fstatfs64, sys_llseek, sys_mlock, sys_munlock, sys_mlockall
/*240*/	.long sys_munlockall, sys_sched_setparam, sys_sched_getparam, sys_sched_setscheduler, sys_sched_getscheduler
/*245*/	.long sys_sched_yield, sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, sys_nanosleep
/*250*/	.long sys_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_ni_syscall
/*255*/	.long sys_sync_file_range, sys_clock_settime, sys_clock_gettime, sys_clock_getres, sys_clock_nanosleep
/*260*/	.long sys_sched_getaffinity, sys_sched_setaffinity, sys_timer_settime, sys_timer_gettime, sys_timer_getoverrun
/*265*/	.long sys_timer_delete, sys_timer_create, sys_nis_syscall, sys_io_setup, sys_io_destroy
/*270*/	.long sys_io_submit, sys_io_cancel, sys_io_getevents, sys_mq_open, sys_mq_unlink
/*275*/	.long sys_mq_timedsend, sys_mq_timedreceive, sys_mq_notify, sys_mq_getsetattr, sys_waitid
/*280*/	.long sys_tee, sys_add_key, sys_request_key, sys_keyctl, sys_openat
/*285*/	.long sys_mkdirat, sys_mknodat, sys_fchownat, sys_futimesat, sys_fstatat64
/*290*/	.long sys_unlinkat, sys_renameat, sys_linkat, sys_symlinkat, sys_readlinkat
/*295*/	.long sys_fchmodat, sys_faccessat, sys_pselect6, sys_ppoll, sys_unshare
/*300*/	.long sys_set_robust_list, sys_get_robust_list, sys_migrate_pages, sys_mbind, sys_get_mempolicy
/*305*/	.long sys_set_mempolicy, sys_kexec_load, sys_move_pages, sys_getcpu, sys_epoll_pwait
/*310*/	.long sys_utimensat, sys_signalfd, sys_timerfd_create, sys_eventfd, sys_fallocate
/*315*/	.long sys_timerfd_settime, sys_timerfd_gettime, sys_signalfd4, sys_eventfd2, sys_epoll_create1
/*320*/	.long sys_dup3, sys_pipe2, sys_inotify_init1, sys_accept4, sys_preadv
/*325*/	.long sys_pwritev, sys_rt_tgsigqueueinfo, sys_perf_event_open, sys_recvmmsg, sys_fanotify_init
/*330*/	.long sys_fanotify_mark, sys_prlimit64, sys_name_to_handle_at, sys_open_by_handle_at, sys_clock_adjtime
/*335*/	.long sys_syncfs, sys_sendmmsg, sys_setns
