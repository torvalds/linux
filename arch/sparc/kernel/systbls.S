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
/*15*/	.long sys_chmod, sys_lchown16, sparc_brk, sys_nis_syscall, sys_lseek
/*20*/	.long sys_getpid, sys_capget, sys_capset, sys_setuid16, sys_getuid16
/*25*/	.long sys_vmsplice, sys_ptrace, sys_alarm, sys_sigaltstack, sys_pause
/*30*/	.long sys_utime, sys_lchown, sys_fchown, sys_access, sys_nice
/*35*/	.long sys_chown, sys_sync, sys_kill, sys_newstat, sys_sendfile
/*40*/	.long sys_newlstat, sys_dup, sys_pipe, sys_times, sys_getuid
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
/*200*/	.long sys_ssetmask, sys_sigsuspend, sys_newlstat, sys_uselib, old_readdir
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
/*250*/	.long sparc_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_nfsservctl
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
/*310*/	.long sys_utimensat, sys_signalfd, sys_ni_syscall, sys_eventfd, sys_fallocate

#ifdef CONFIG_SUNOS_EMUL
	/* Now the SunOS syscall table. */

	.align 4
	.globl sunos_sys_table
sunos_sys_table:
/*0*/	.long sunos_indir, sys_exit, sys_fork
	.long sunos_read, sunos_write, sys_open
	.long sys_close, sunos_wait4, sys_creat
	.long sys_link, sys_unlink, sunos_execv
	.long sys_chdir, sunos_nosys, sys_mknod
	.long sys_chmod, sys_lchown16, sunos_brk
	.long sunos_nosys, sys_lseek, sunos_getpid
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_getuid, sunos_nosys, sys_ptrace
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sys_access, sunos_nosys, sunos_nosys
	.long sys_sync, sys_kill, sys_newstat
	.long sunos_nosys, sys_newlstat, sys_dup
	.long sys_pipe, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_getgid
	.long sunos_nosys, sunos_nosys
/*50*/	.long sunos_nosys, sys_acct, sunos_nosys
	.long sunos_mctl, sunos_ioctl, sys_reboot
	.long sunos_nosys, sys_symlink, sys_readlink
	.long sys_execve, sys_umask, sys_chroot
	.long sys_newfstat, sunos_nosys, sys_getpagesize
	.long sys_msync, sys_vfork, sunos_nosys
	.long sunos_nosys, sunos_sbrk, sunos_sstk
	.long sunos_mmap, sunos_vadvise, sys_munmap
	.long sys_mprotect, sys_madvise, sys_vhangup
	.long sunos_nosys, sys_mincore, sys_getgroups16
	.long sys_setgroups16, sys_getpgrp, sunos_setpgrp
	.long sys_setitimer, sunos_nosys, sys_swapon
	.long sys_getitimer, sys_gethostname, sys_sethostname
	.long sunos_getdtablesize, sys_dup2, sunos_nop
	.long sys_fcntl, sunos_select, sunos_nop
	.long sys_fsync, sys_setpriority, sys_socket
	.long sys_connect, sunos_accept
/*100*/	.long sys_getpriority, sunos_send, sunos_recv
	.long sunos_nosys, sys_bind, sunos_setsockopt
	.long sys_listen, sunos_nosys, sunos_sigaction
	.long sunos_sigblock, sunos_sigsetmask, sys_sigpause
	.long sys_sigstack, sys_recvmsg, sys_sendmsg
	.long sunos_nosys, sys_gettimeofday, sys_getrusage
	.long sunos_getsockopt, sunos_nosys, sunos_readv
	.long sunos_writev, sys_settimeofday, sys_fchown16
	.long sys_fchmod, sys_recvfrom, sys_setreuid16
	.long sys_setregid16, sys_rename, sys_truncate
	.long sys_ftruncate, sys_flock, sunos_nosys
	.long sys_sendto, sys_shutdown, sys_socketpair
	.long sys_mkdir, sys_rmdir, sys_utimes
	.long sys_sigreturn, sunos_nosys, sys_getpeername
	.long sunos_gethostid, sunos_nosys, sys_getrlimit
	.long sys_setrlimit, sunos_killpg, sunos_nosys
	.long sunos_nosys, sunos_nosys
/*150*/	.long sys_getsockname, sunos_nosys, sunos_nosys
	.long sys_poll, sunos_nosys, sunos_nosys
	.long sunos_getdirentries, sys_statfs, sys_fstatfs
	.long sys_oldumount, sunos_nosys, sunos_nosys
	.long sys_getdomainname, sys_setdomainname
	.long sunos_nosys, sys_quotactl, sunos_nosys
	.long sunos_mount, sys_ustat, sunos_semsys
	.long sunos_msgsys, sunos_shmsys, sunos_audit
	.long sunos_nosys, sunos_getdents, sys_setsid
	.long sys_fchdir, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sys_sigpending, sunos_nosys
	.long sys_setpgid, sunos_pathconf, sunos_fpathconf
	.long sunos_sysconf, sunos_uname, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
/*200*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys
/*250*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*260*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*270*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*280*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*290*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*300*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys
/*310*/	.long sunos_nosys, sunos_nosys, sunos_nosys
	.long sunos_nosys, sunos_nosys

#endif
