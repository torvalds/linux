/*
 * definitions for sys_call_table, each line represents an
 * entry in the table in the form
 * SYSCALL(64 bit syscall, 31 bit emulated syscall)
 *
 * this file is meant to be included from entry.S
 */

#define NI_SYSCALL SYSCALL(sys_ni_syscall,sys_ni_syscall)

NI_SYSCALL						/* 0 */
SYSCALL(sys_exit,sys_exit)
SYSCALL(sys_fork,sys_fork)
SYSCALL(sys_read,compat_sys_s390_read)
SYSCALL(sys_write,compat_sys_s390_write)
SYSCALL(sys_open,compat_sys_open)			/* 5 */
SYSCALL(sys_close,sys_close)
SYSCALL(sys_restart_syscall,sys_restart_syscall)
SYSCALL(sys_creat,compat_sys_creat)
SYSCALL(sys_link,compat_sys_link)
SYSCALL(sys_unlink,compat_sys_unlink)			/* 10 */
SYSCALL(sys_execve,compat_sys_execve)
SYSCALL(sys_chdir,compat_sys_chdir)
SYSCALL(sys_ni_syscall,compat_sys_time)			/* old time syscall */
SYSCALL(sys_mknod,compat_sys_mknod)
SYSCALL(sys_chmod,compat_sys_chmod)			/* 15 */
SYSCALL(sys_ni_syscall,compat_sys_s390_lchown16)	/* old lchown16 syscall*/
NI_SYSCALL						/* old break syscall holder */
NI_SYSCALL						/* old stat syscall holder */
SYSCALL(sys_lseek,compat_sys_lseek)
SYSCALL(sys_getpid,sys_getpid)				/* 20 */
SYSCALL(sys_mount,compat_sys_mount)
SYSCALL(sys_oldumount,compat_sys_oldumount)
SYSCALL(sys_ni_syscall,compat_sys_s390_setuid16)	/* old setuid16 syscall*/
SYSCALL(sys_ni_syscall,compat_sys_s390_getuid16)	/* old getuid16 syscall*/
SYSCALL(sys_ni_syscall,compat_sys_stime)		/* 25 old stime syscall */
SYSCALL(sys_ptrace,compat_sys_ptrace)
SYSCALL(sys_alarm,sys_alarm)
NI_SYSCALL						/* old fstat syscall */
SYSCALL(sys_pause,sys_pause)
SYSCALL(sys_utime,compat_sys_utime)			/* 30 */
NI_SYSCALL						/* old stty syscall */
NI_SYSCALL						/* old gtty syscall */
SYSCALL(sys_access,compat_sys_access)
SYSCALL(sys_nice,sys_nice)
NI_SYSCALL						/* 35 old ftime syscall */
SYSCALL(sys_sync,sys_sync)
SYSCALL(sys_kill,sys_kill)
SYSCALL(sys_rename,compat_sys_rename)
SYSCALL(sys_mkdir,compat_sys_mkdir)
SYSCALL(sys_rmdir,compat_sys_rmdir)			/* 40 */
SYSCALL(sys_dup,sys_dup)
SYSCALL(sys_pipe,compat_sys_pipe)
SYSCALL(sys_times,compat_sys_times)
NI_SYSCALL						/* old prof syscall */
SYSCALL(sys_brk,compat_sys_brk)				/* 45 */
SYSCALL(sys_ni_syscall,compat_sys_s390_setgid16)	/* old setgid16 syscall*/
SYSCALL(sys_ni_syscall,compat_sys_s390_getgid16)	/* old getgid16 syscall*/
SYSCALL(sys_signal,compat_sys_signal)
SYSCALL(sys_ni_syscall,compat_sys_s390_geteuid16)	/* old geteuid16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_getegid16)	/* 50 old getegid16 syscall */
SYSCALL(sys_acct,compat_sys_acct)
SYSCALL(sys_umount,compat_sys_umount)
NI_SYSCALL						/* old lock syscall */
SYSCALL(sys_ioctl,compat_sys_ioctl)
SYSCALL(sys_fcntl,compat_sys_fcntl)			/* 55 */
NI_SYSCALL						/* intel mpx syscall */
SYSCALL(sys_setpgid,sys_setpgid)
NI_SYSCALL						/* old ulimit syscall */
NI_SYSCALL						/* old uname syscall */
SYSCALL(sys_umask,sys_umask)				/* 60 */
SYSCALL(sys_chroot,compat_sys_chroot)
SYSCALL(sys_ustat,compat_sys_ustat)
SYSCALL(sys_dup2,sys_dup2)
SYSCALL(sys_getppid,sys_getppid)
SYSCALL(sys_getpgrp,sys_getpgrp)			/* 65 */
SYSCALL(sys_setsid,sys_setsid)
SYSCALL(sys_sigaction,compat_sys_sigaction)
NI_SYSCALL						/* old sgetmask syscall*/
NI_SYSCALL						/* old ssetmask syscall*/
SYSCALL(sys_ni_syscall,compat_sys_s390_setreuid16)	/* old setreuid16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_setregid16)	/* old setregid16 syscall */
SYSCALL(sys_sigsuspend,compat_sys_sigsuspend)
SYSCALL(sys_sigpending,compat_sys_sigpending)
SYSCALL(sys_sethostname,compat_sys_sethostname)
SYSCALL(sys_setrlimit,compat_sys_setrlimit)		/* 75 */
SYSCALL(sys_getrlimit,compat_sys_old_getrlimit)
SYSCALL(sys_getrusage,compat_sys_getrusage)
SYSCALL(sys_gettimeofday,compat_sys_gettimeofday)
SYSCALL(sys_settimeofday,compat_sys_settimeofday)
SYSCALL(sys_ni_syscall,compat_sys_s390_getgroups16)	/* 80 old getgroups16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_setgroups16)	/* old setgroups16 syscall */
NI_SYSCALL						/* old select syscall */
SYSCALL(sys_symlink,compat_sys_symlink)
NI_SYSCALL						/* old lstat syscall */
SYSCALL(sys_readlink,compat_sys_readlink)		/* 85 */
SYSCALL(sys_uselib,compat_sys_uselib)
SYSCALL(sys_swapon,compat_sys_swapon)
SYSCALL(sys_reboot,compat_sys_reboot)
SYSCALL(sys_ni_syscall,compat_sys_old_readdir)		/* old readdir syscall */
SYSCALL(sys_old_mmap,compat_sys_s390_old_mmap)		/* 90 */
SYSCALL(sys_munmap,compat_sys_munmap)
SYSCALL(sys_truncate,compat_sys_truncate)
SYSCALL(sys_ftruncate,compat_sys_ftruncate)
SYSCALL(sys_fchmod,sys_fchmod)
SYSCALL(sys_ni_syscall,compat_sys_s390_fchown16)	/* 95 old fchown16 syscall*/
SYSCALL(sys_getpriority,sys_getpriority)
SYSCALL(sys_setpriority,sys_setpriority)
NI_SYSCALL						/* old profil syscall */
SYSCALL(sys_statfs,compat_sys_statfs)
SYSCALL(sys_fstatfs,compat_sys_fstatfs)			/* 100 */
NI_SYSCALL						/* ioperm for i386 */
SYSCALL(sys_socketcall,compat_sys_socketcall)
SYSCALL(sys_syslog,compat_sys_syslog)
SYSCALL(sys_setitimer,compat_sys_setitimer)
SYSCALL(sys_getitimer,compat_sys_getitimer)		/* 105 */
SYSCALL(sys_newstat,compat_sys_newstat)
SYSCALL(sys_newlstat,compat_sys_newlstat)
SYSCALL(sys_newfstat,compat_sys_newfstat)
NI_SYSCALL						/* old uname syscall */
SYSCALL(sys_lookup_dcookie,compat_sys_lookup_dcookie)	/* 110 */
SYSCALL(sys_vhangup,sys_vhangup)
NI_SYSCALL						/* old "idle" system call */
NI_SYSCALL						/* vm86old for i386 */
SYSCALL(sys_wait4,compat_sys_wait4)
SYSCALL(sys_swapoff,compat_sys_swapoff)			/* 115 */
SYSCALL(sys_sysinfo,compat_sys_sysinfo)
SYSCALL(sys_s390_ipc,compat_sys_s390_ipc)
SYSCALL(sys_fsync,sys_fsync)
SYSCALL(sys_sigreturn,compat_sys_sigreturn)
SYSCALL(sys_clone,compat_sys_clone)			/* 120 */
SYSCALL(sys_setdomainname,compat_sys_setdomainname)
SYSCALL(sys_newuname,compat_sys_newuname)
NI_SYSCALL						/* modify_ldt for i386 */
SYSCALL(sys_adjtimex,compat_sys_adjtimex)
SYSCALL(sys_mprotect,compat_sys_mprotect)		/* 125 */
SYSCALL(sys_sigprocmask,compat_sys_sigprocmask)
NI_SYSCALL						/* old "create module" */
SYSCALL(sys_init_module,compat_sys_init_module)
SYSCALL(sys_delete_module,compat_sys_delete_module)
NI_SYSCALL						/* 130: old get_kernel_syms */
SYSCALL(sys_quotactl,compat_sys_quotactl)
SYSCALL(sys_getpgid,sys_getpgid)
SYSCALL(sys_fchdir,sys_fchdir)
SYSCALL(sys_bdflush,compat_sys_bdflush)
SYSCALL(sys_sysfs,compat_sys_sysfs)			/* 135 */
SYSCALL(sys_s390_personality,sys_s390_personality)
NI_SYSCALL						/* for afs_syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_setfsuid16)	/* old setfsuid16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_setfsgid16)	/* old setfsgid16 syscall */
SYSCALL(sys_llseek,compat_sys_llseek)			/* 140 */
SYSCALL(sys_getdents,compat_sys_getdents)
SYSCALL(sys_select,compat_sys_select)
SYSCALL(sys_flock,sys_flock)
SYSCALL(sys_msync,compat_sys_msync)
SYSCALL(sys_readv,compat_sys_readv)			/* 145 */
SYSCALL(sys_writev,compat_sys_writev)
SYSCALL(sys_getsid,sys_getsid)
SYSCALL(sys_fdatasync,sys_fdatasync)
SYSCALL(sys_sysctl,compat_sys_sysctl)
SYSCALL(sys_mlock,compat_sys_mlock)			/* 150 */
SYSCALL(sys_munlock,compat_sys_munlock)
SYSCALL(sys_mlockall,sys_mlockall)
SYSCALL(sys_munlockall,sys_munlockall)
SYSCALL(sys_sched_setparam,compat_sys_sched_setparam)
SYSCALL(sys_sched_getparam,compat_sys_sched_getparam)	/* 155 */
SYSCALL(sys_sched_setscheduler,compat_sys_sched_setscheduler)
SYSCALL(sys_sched_getscheduler,sys_sched_getscheduler)
SYSCALL(sys_sched_yield,sys_sched_yield)
SYSCALL(sys_sched_get_priority_max,sys_sched_get_priority_max)
SYSCALL(sys_sched_get_priority_min,sys_sched_get_priority_min)	/* 160 */
SYSCALL(sys_sched_rr_get_interval,compat_sys_sched_rr_get_interval)
SYSCALL(sys_nanosleep,compat_sys_nanosleep)
SYSCALL(sys_mremap,compat_sys_mremap)
SYSCALL(sys_ni_syscall,compat_sys_s390_setresuid16)	/* old setresuid16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_getresuid16)	/* 165 old getresuid16 syscall */
NI_SYSCALL						/* for vm86 */
NI_SYSCALL						/* old sys_query_module */
SYSCALL(sys_poll,compat_sys_poll)
NI_SYSCALL						/* old nfsservctl */
SYSCALL(sys_ni_syscall,compat_sys_s390_setresgid16)	/* 170 old setresgid16 syscall */
SYSCALL(sys_ni_syscall,compat_sys_s390_getresgid16)	/* old getresgid16 syscall */
SYSCALL(sys_prctl,compat_sys_prctl)
SYSCALL(sys_rt_sigreturn,compat_sys_rt_sigreturn)
SYSCALL(sys_rt_sigaction,compat_sys_rt_sigaction)
SYSCALL(sys_rt_sigprocmask,compat_sys_rt_sigprocmask)	/* 175 */
SYSCALL(sys_rt_sigpending,compat_sys_rt_sigpending)
SYSCALL(sys_rt_sigtimedwait,compat_sys_rt_sigtimedwait)
SYSCALL(sys_rt_sigqueueinfo,compat_sys_rt_sigqueueinfo)
SYSCALL(sys_rt_sigsuspend,compat_sys_rt_sigsuspend)
SYSCALL(sys_pread64,compat_sys_s390_pread64)		/* 180 */
SYSCALL(sys_pwrite64,compat_sys_s390_pwrite64)
SYSCALL(sys_ni_syscall,compat_sys_s390_chown16)		/* old chown16 syscall */
SYSCALL(sys_getcwd,compat_sys_getcwd)
SYSCALL(sys_capget,compat_sys_capget)
SYSCALL(sys_capset,compat_sys_capset)			/* 185 */
SYSCALL(sys_sigaltstack,compat_sys_sigaltstack)
SYSCALL(sys_sendfile64,compat_sys_sendfile)
NI_SYSCALL						/* streams1 */
NI_SYSCALL						/* streams2 */
SYSCALL(sys_vfork,sys_vfork)				/* 190 */
SYSCALL(sys_getrlimit,compat_sys_getrlimit)
SYSCALL(sys_mmap2,compat_sys_s390_mmap2)
SYSCALL(sys_ni_syscall,compat_sys_s390_truncate64)
SYSCALL(sys_ni_syscall,compat_sys_s390_ftruncate64)
SYSCALL(sys_ni_syscall,compat_sys_s390_stat64)		/* 195 */
SYSCALL(sys_ni_syscall,compat_sys_s390_lstat64)
SYSCALL(sys_ni_syscall,compat_sys_s390_fstat64)
SYSCALL(sys_lchown,compat_sys_lchown)
SYSCALL(sys_getuid,sys_getuid)
SYSCALL(sys_getgid,sys_getgid)				/* 200 */
SYSCALL(sys_geteuid,sys_geteuid)
SYSCALL(sys_getegid,sys_getegid)
SYSCALL(sys_setreuid,sys_setreuid)
SYSCALL(sys_setregid,sys_setregid)
SYSCALL(sys_getgroups,compat_sys_getgroups)		/* 205 */
SYSCALL(sys_setgroups,compat_sys_setgroups)
SYSCALL(sys_fchown,sys_fchown)
SYSCALL(sys_setresuid,sys_setresuid)
SYSCALL(sys_getresuid,compat_sys_getresuid)
SYSCALL(sys_setresgid,sys_setresgid)			/* 210 */
SYSCALL(sys_getresgid,compat_sys_getresgid)
SYSCALL(sys_chown,compat_sys_chown)
SYSCALL(sys_setuid,sys_setuid)
SYSCALL(sys_setgid,sys_setgid)
SYSCALL(sys_setfsuid,sys_setfsuid)			/* 215 */
SYSCALL(sys_setfsgid,sys_setfsgid)
SYSCALL(sys_pivot_root,compat_sys_pivot_root)
SYSCALL(sys_mincore,compat_sys_mincore)
SYSCALL(sys_madvise,compat_sys_madvise)
SYSCALL(sys_getdents64,compat_sys_getdents64)		/* 220 */
SYSCALL(sys_ni_syscall,compat_sys_fcntl64)
SYSCALL(sys_readahead,compat_sys_s390_readahead)
SYSCALL(sys_ni_syscall,compat_sys_sendfile64)
SYSCALL(sys_setxattr,compat_sys_setxattr)
SYSCALL(sys_lsetxattr,compat_sys_lsetxattr)		/* 225 */
SYSCALL(sys_fsetxattr,compat_sys_fsetxattr)
SYSCALL(sys_getxattr,compat_sys_getxattr)
SYSCALL(sys_lgetxattr,compat_sys_lgetxattr)
SYSCALL(sys_fgetxattr,compat_sys_fgetxattr)
SYSCALL(sys_listxattr,compat_sys_listxattr)		/* 230 */
SYSCALL(sys_llistxattr,compat_sys_llistxattr)
SYSCALL(sys_flistxattr,compat_sys_flistxattr)
SYSCALL(sys_removexattr,compat_sys_removexattr)
SYSCALL(sys_lremovexattr,compat_sys_lremovexattr)
SYSCALL(sys_fremovexattr,compat_sys_fremovexattr)	/* 235 */
SYSCALL(sys_gettid,sys_gettid)
SYSCALL(sys_tkill,sys_tkill)
SYSCALL(sys_futex,compat_sys_futex)
SYSCALL(sys_sched_setaffinity,compat_sys_sched_setaffinity)
SYSCALL(sys_sched_getaffinity,compat_sys_sched_getaffinity)	/* 240 */
SYSCALL(sys_tgkill,sys_tgkill)
NI_SYSCALL						/* reserved for TUX */
SYSCALL(sys_io_setup,compat_sys_io_setup)
SYSCALL(sys_io_destroy,compat_sys_io_destroy)
SYSCALL(sys_io_getevents,compat_sys_io_getevents)	/* 245 */
SYSCALL(sys_io_submit,compat_sys_io_submit)
SYSCALL(sys_io_cancel,compat_sys_io_cancel)
SYSCALL(sys_exit_group,sys_exit_group)
SYSCALL(sys_epoll_create,sys_epoll_create)
SYSCALL(sys_epoll_ctl,compat_sys_epoll_ctl)		/* 250 */
SYSCALL(sys_epoll_wait,compat_sys_epoll_wait)
SYSCALL(sys_set_tid_address,compat_sys_set_tid_address)
SYSCALL(sys_fadvise64_64,compat_sys_s390_fadvise64)
SYSCALL(sys_timer_create,compat_sys_timer_create)
SYSCALL(sys_timer_settime,compat_sys_timer_settime)	/* 255 */
SYSCALL(sys_timer_gettime,compat_sys_timer_gettime)
SYSCALL(sys_timer_getoverrun,sys_timer_getoverrun)
SYSCALL(sys_timer_delete,sys_timer_delete)
SYSCALL(sys_clock_settime,compat_sys_clock_settime)
SYSCALL(sys_clock_gettime,compat_sys_clock_gettime)	/* 260 */
SYSCALL(sys_clock_getres,compat_sys_clock_getres)
SYSCALL(sys_clock_nanosleep,compat_sys_clock_nanosleep)
NI_SYSCALL						/* reserved for vserver */
SYSCALL(sys_ni_syscall,compat_sys_s390_fadvise64_64)
SYSCALL(sys_statfs64,compat_sys_statfs64)
SYSCALL(sys_fstatfs64,compat_sys_fstatfs64)
SYSCALL(sys_remap_file_pages,compat_sys_remap_file_pages)
SYSCALL(sys_mbind,compat_sys_mbind)
SYSCALL(sys_get_mempolicy,compat_sys_get_mempolicy)
SYSCALL(sys_set_mempolicy,compat_sys_set_mempolicy)
SYSCALL(sys_mq_open,compat_sys_mq_open)
SYSCALL(sys_mq_unlink,compat_sys_mq_unlink)
SYSCALL(sys_mq_timedsend,compat_sys_mq_timedsend)
SYSCALL(sys_mq_timedreceive,compat_sys_mq_timedreceive)
SYSCALL(sys_mq_notify,compat_sys_mq_notify)		/* 275 */
SYSCALL(sys_mq_getsetattr,compat_sys_mq_getsetattr)
SYSCALL(sys_kexec_load,compat_sys_kexec_load)
SYSCALL(sys_add_key,compat_sys_add_key)
SYSCALL(sys_request_key,compat_sys_request_key)
SYSCALL(sys_keyctl,compat_sys_keyctl)			/* 280 */
SYSCALL(sys_waitid,compat_sys_waitid)
SYSCALL(sys_ioprio_set,sys_ioprio_set)
SYSCALL(sys_ioprio_get,sys_ioprio_get)
SYSCALL(sys_inotify_init,sys_inotify_init)
SYSCALL(sys_inotify_add_watch,compat_sys_inotify_add_watch)	/* 285 */
SYSCALL(sys_inotify_rm_watch,sys_inotify_rm_watch)
SYSCALL(sys_migrate_pages,compat_sys_migrate_pages)
SYSCALL(sys_openat,compat_sys_openat)
SYSCALL(sys_mkdirat,compat_sys_mkdirat)
SYSCALL(sys_mknodat,compat_sys_mknodat)			/* 290 */
SYSCALL(sys_fchownat,compat_sys_fchownat)
SYSCALL(sys_futimesat,compat_sys_futimesat)
SYSCALL(sys_newfstatat,compat_sys_s390_fstatat64)
SYSCALL(sys_unlinkat,compat_sys_unlinkat)
SYSCALL(sys_renameat,compat_sys_renameat)		/* 295 */
SYSCALL(sys_linkat,compat_sys_linkat)
SYSCALL(sys_symlinkat,compat_sys_symlinkat)
SYSCALL(sys_readlinkat,compat_sys_readlinkat)
SYSCALL(sys_fchmodat,compat_sys_fchmodat)
SYSCALL(sys_faccessat,compat_sys_faccessat)		/* 300 */
SYSCALL(sys_pselect6,compat_sys_pselect6)
SYSCALL(sys_ppoll,compat_sys_ppoll)
SYSCALL(sys_unshare,compat_sys_unshare)
SYSCALL(sys_set_robust_list,compat_sys_set_robust_list)
SYSCALL(sys_get_robust_list,compat_sys_get_robust_list)
SYSCALL(sys_splice,compat_sys_splice)
SYSCALL(sys_sync_file_range,compat_sys_s390_sync_file_range)
SYSCALL(sys_tee,compat_sys_tee)
SYSCALL(sys_vmsplice,compat_sys_vmsplice)
SYSCALL(sys_move_pages,compat_sys_move_pages)
SYSCALL(sys_getcpu,compat_sys_getcpu)
SYSCALL(sys_epoll_pwait,compat_sys_epoll_pwait)
SYSCALL(sys_utimes,compat_sys_utimes)
SYSCALL(sys_fallocate,compat_sys_s390_fallocate)
SYSCALL(sys_utimensat,compat_sys_utimensat)		/* 315 */
SYSCALL(sys_signalfd,compat_sys_signalfd)
NI_SYSCALL						/* 317 old sys_timer_fd */
SYSCALL(sys_eventfd,sys_eventfd)
SYSCALL(sys_timerfd_create,sys_timerfd_create)
SYSCALL(sys_timerfd_settime,compat_sys_timerfd_settime) /* 320 */
SYSCALL(sys_timerfd_gettime,compat_sys_timerfd_gettime)
SYSCALL(sys_signalfd4,compat_sys_signalfd4)
SYSCALL(sys_eventfd2,sys_eventfd2)
SYSCALL(sys_inotify_init1,sys_inotify_init1)
SYSCALL(sys_pipe2,compat_sys_pipe2)			/* 325 */
SYSCALL(sys_dup3,sys_dup3)
SYSCALL(sys_epoll_create1,sys_epoll_create1)
SYSCALL(sys_preadv,compat_sys_preadv)
SYSCALL(sys_pwritev,compat_sys_pwritev)
SYSCALL(sys_rt_tgsigqueueinfo,compat_sys_rt_tgsigqueueinfo) /* 330 */
SYSCALL(sys_perf_event_open,compat_sys_perf_event_open)
SYSCALL(sys_fanotify_init,sys_fanotify_init)
SYSCALL(sys_fanotify_mark,compat_sys_fanotify_mark)
SYSCALL(sys_prlimit64,compat_sys_prlimit64)
SYSCALL(sys_name_to_handle_at,compat_sys_name_to_handle_at) /* 335 */
SYSCALL(sys_open_by_handle_at,compat_sys_open_by_handle_at)
SYSCALL(sys_clock_adjtime,compat_sys_clock_adjtime)
SYSCALL(sys_syncfs,sys_syncfs)
SYSCALL(sys_setns,sys_setns)
SYSCALL(sys_process_vm_readv,compat_sys_process_vm_readv) /* 340 */
SYSCALL(sys_process_vm_writev,compat_sys_process_vm_writev)
SYSCALL(sys_s390_runtime_instr,sys_s390_runtime_instr)
SYSCALL(sys_kcmp,compat_sys_kcmp)
SYSCALL(sys_finit_module,compat_sys_finit_module)
SYSCALL(sys_sched_setattr,compat_sys_sched_setattr)	/* 345 */
SYSCALL(sys_sched_getattr,compat_sys_sched_getattr)
SYSCALL(sys_renameat2,compat_sys_renameat2)
SYSCALL(sys_seccomp,compat_sys_seccomp)
SYSCALL(sys_getrandom,compat_sys_getrandom)
SYSCALL(sys_memfd_create,compat_sys_memfd_create)	/* 350 */
SYSCALL(sys_bpf,compat_sys_bpf)
SYSCALL(sys_s390_pci_mmio_write,compat_sys_s390_pci_mmio_write)
SYSCALL(sys_s390_pci_mmio_read,compat_sys_s390_pci_mmio_read)
SYSCALL(sys_execveat,compat_sys_execveat)
SYSCALL(sys_userfaultfd,sys_userfaultfd)		/* 355 */
SYSCALL(sys_membarrier,sys_membarrier)
SYSCALL(sys_recvmmsg,compat_sys_recvmmsg)
SYSCALL(sys_sendmmsg,compat_sys_sendmmsg)
SYSCALL(sys_socket,sys_socket)
SYSCALL(sys_socketpair,compat_sys_socketpair)		/* 360 */
SYSCALL(sys_bind,sys_bind)
SYSCALL(sys_connect,sys_connect)
SYSCALL(sys_listen,sys_listen)
SYSCALL(sys_accept4,sys_accept4)
SYSCALL(sys_getsockopt,compat_sys_getsockopt)		/* 365 */
SYSCALL(sys_setsockopt,compat_sys_setsockopt)
SYSCALL(sys_getsockname,compat_sys_getsockname)
SYSCALL(sys_getpeername,compat_sys_getpeername)
SYSCALL(sys_sendto,compat_sys_sendto)
SYSCALL(sys_sendmsg,compat_sys_sendmsg)			/* 370 */
SYSCALL(sys_recvfrom,compat_sys_recvfrom)
SYSCALL(sys_recvmsg,compat_sys_recvmsg)
SYSCALL(sys_shutdown,sys_shutdown)
SYSCALL(sys_mlock2,compat_sys_mlock2)
SYSCALL(sys_copy_file_range,compat_sys_copy_file_range) /* 375 */
SYSCALL(sys_preadv2,compat_sys_preadv2)
SYSCALL(sys_pwritev2,compat_sys_pwritev2)
SYSCALL(sys_s390_guarded_storage,compat_sys_s390_guarded_storage) /* 378 */
SYSCALL(sys_statx,compat_sys_statx)
