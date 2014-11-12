/*
 * definitions for sys_call_table, each line represents an
 * entry in the table in the form 
 * SYSCALL(31 bit syscall, 64 bit syscall, 31 bit emulated syscall)
 *
 * this file is meant to be included from entry.S and entry64.S
 */

#define NI_SYSCALL SYSCALL(sys_ni_syscall,sys_ni_syscall,sys_ni_syscall)

NI_SYSCALL							/* 0 */
SYSCALL(sys_exit,sys_exit,compat_sys_exit)
SYSCALL(sys_fork,sys_fork,sys_fork)
SYSCALL(sys_read,sys_read,compat_sys_s390_read)
SYSCALL(sys_write,sys_write,compat_sys_s390_write)
SYSCALL(sys_open,sys_open,compat_sys_open)			/* 5 */
SYSCALL(sys_close,sys_close,compat_sys_close)
SYSCALL(sys_restart_syscall,sys_restart_syscall,sys_restart_syscall)
SYSCALL(sys_creat,sys_creat,compat_sys_creat)
SYSCALL(sys_link,sys_link,compat_sys_link)
SYSCALL(sys_unlink,sys_unlink,compat_sys_unlink)		/* 10 */
SYSCALL(sys_execve,sys_execve,compat_sys_execve)
SYSCALL(sys_chdir,sys_chdir,compat_sys_chdir)
SYSCALL(sys_time,sys_ni_syscall,compat_sys_time)		/* old time syscall */
SYSCALL(sys_mknod,sys_mknod,compat_sys_mknod)
SYSCALL(sys_chmod,sys_chmod,compat_sys_chmod)			/* 15 */
SYSCALL(sys_lchown16,sys_ni_syscall,compat_sys_s390_lchown16)	/* old lchown16 syscall*/
NI_SYSCALL							/* old break syscall holder */
NI_SYSCALL							/* old stat syscall holder */
SYSCALL(sys_lseek,sys_lseek,compat_sys_lseek)
SYSCALL(sys_getpid,sys_getpid,sys_getpid)			/* 20 */
SYSCALL(sys_mount,sys_mount,compat_sys_mount)
SYSCALL(sys_oldumount,sys_oldumount,compat_sys_oldumount)
SYSCALL(sys_setuid16,sys_ni_syscall,compat_sys_s390_setuid16)	/* old setuid16 syscall*/
SYSCALL(sys_getuid16,sys_ni_syscall,compat_sys_s390_getuid16)	/* old getuid16 syscall*/
SYSCALL(sys_stime,sys_ni_syscall,compat_sys_stime)		/* 25 old stime syscall */
SYSCALL(sys_ptrace,sys_ptrace,compat_sys_ptrace)
SYSCALL(sys_alarm,sys_alarm,compat_sys_alarm)
NI_SYSCALL							/* old fstat syscall */
SYSCALL(sys_pause,sys_pause,sys_pause)
SYSCALL(sys_utime,sys_utime,compat_sys_utime)		/* 30 */
NI_SYSCALL							/* old stty syscall */
NI_SYSCALL							/* old gtty syscall */
SYSCALL(sys_access,sys_access,compat_sys_access)
SYSCALL(sys_nice,sys_nice,compat_sys_nice)
NI_SYSCALL							/* 35 old ftime syscall */
SYSCALL(sys_sync,sys_sync,sys_sync)
SYSCALL(sys_kill,sys_kill,compat_sys_kill)
SYSCALL(sys_rename,sys_rename,compat_sys_rename)
SYSCALL(sys_mkdir,sys_mkdir,compat_sys_mkdir)
SYSCALL(sys_rmdir,sys_rmdir,compat_sys_rmdir)		/* 40 */
SYSCALL(sys_dup,sys_dup,compat_sys_dup)
SYSCALL(sys_pipe,sys_pipe,compat_sys_pipe)
SYSCALL(sys_times,sys_times,compat_sys_times)
NI_SYSCALL							/* old prof syscall */
SYSCALL(sys_brk,sys_brk,compat_sys_brk)				/* 45 */
SYSCALL(sys_setgid16,sys_ni_syscall,compat_sys_s390_setgid16)	/* old setgid16 syscall*/
SYSCALL(sys_getgid16,sys_ni_syscall,compat_sys_s390_getgid16)	/* old getgid16 syscall*/
SYSCALL(sys_signal,sys_signal,compat_sys_signal)
SYSCALL(sys_geteuid16,sys_ni_syscall,compat_sys_s390_geteuid16)	/* old geteuid16 syscall */
SYSCALL(sys_getegid16,sys_ni_syscall,compat_sys_s390_getegid16)	/* 50 old getegid16 syscall */
SYSCALL(sys_acct,sys_acct,compat_sys_acct)
SYSCALL(sys_umount,sys_umount,compat_sys_umount)
NI_SYSCALL							/* old lock syscall */
SYSCALL(sys_ioctl,sys_ioctl,compat_sys_ioctl)
SYSCALL(sys_fcntl,sys_fcntl,compat_sys_fcntl)		/* 55 */
NI_SYSCALL							/* intel mpx syscall */
SYSCALL(sys_setpgid,sys_setpgid,compat_sys_setpgid)
NI_SYSCALL							/* old ulimit syscall */
NI_SYSCALL							/* old uname syscall */
SYSCALL(sys_umask,sys_umask,compat_sys_umask)			/* 60 */
SYSCALL(sys_chroot,sys_chroot,compat_sys_chroot)
SYSCALL(sys_ustat,sys_ustat,compat_sys_ustat)
SYSCALL(sys_dup2,sys_dup2,compat_sys_dup2)
SYSCALL(sys_getppid,sys_getppid,sys_getppid)
SYSCALL(sys_getpgrp,sys_getpgrp,sys_getpgrp)			/* 65 */
SYSCALL(sys_setsid,sys_setsid,sys_setsid)
SYSCALL(sys_sigaction,sys_sigaction,compat_sys_sigaction)
NI_SYSCALL							/* old sgetmask syscall*/
NI_SYSCALL							/* old ssetmask syscall*/
SYSCALL(sys_setreuid16,sys_ni_syscall,compat_sys_s390_setreuid16) /* old setreuid16 syscall */
SYSCALL(sys_setregid16,sys_ni_syscall,compat_sys_s390_setregid16) /* old setregid16 syscall */
SYSCALL(sys_sigsuspend,sys_sigsuspend,compat_sys_sigsuspend)
SYSCALL(sys_sigpending,sys_sigpending,compat_sys_sigpending)
SYSCALL(sys_sethostname,sys_sethostname,compat_sys_sethostname)
SYSCALL(sys_setrlimit,sys_setrlimit,compat_sys_setrlimit)	/* 75 */
SYSCALL(sys_old_getrlimit,sys_getrlimit,compat_sys_old_getrlimit)
SYSCALL(sys_getrusage,sys_getrusage,compat_sys_getrusage)
SYSCALL(sys_gettimeofday,sys_gettimeofday,compat_sys_gettimeofday)
SYSCALL(sys_settimeofday,sys_settimeofday,compat_sys_settimeofday)
SYSCALL(sys_getgroups16,sys_ni_syscall,compat_sys_s390_getgroups16)	/* 80 old getgroups16 syscall */
SYSCALL(sys_setgroups16,sys_ni_syscall,compat_sys_s390_setgroups16)	/* old setgroups16 syscall */
NI_SYSCALL							/* old select syscall */
SYSCALL(sys_symlink,sys_symlink,compat_sys_symlink)
NI_SYSCALL							/* old lstat syscall */
SYSCALL(sys_readlink,sys_readlink,compat_sys_readlink)		/* 85 */
SYSCALL(sys_uselib,sys_uselib,compat_sys_uselib)
SYSCALL(sys_swapon,sys_swapon,compat_sys_swapon)
SYSCALL(sys_reboot,sys_reboot,compat_sys_reboot)
SYSCALL(sys_ni_syscall,sys_ni_syscall,compat_sys_old_readdir)	/* old readdir syscall */
SYSCALL(sys_old_mmap,sys_old_mmap,compat_sys_s390_old_mmap)	/* 90 */
SYSCALL(sys_munmap,sys_munmap,compat_sys_munmap)
SYSCALL(sys_truncate,sys_truncate,compat_sys_truncate)
SYSCALL(sys_ftruncate,sys_ftruncate,compat_sys_ftruncate)
SYSCALL(sys_fchmod,sys_fchmod,compat_sys_fchmod)
SYSCALL(sys_fchown16,sys_ni_syscall,compat_sys_s390_fchown16)	/* 95 old fchown16 syscall*/
SYSCALL(sys_getpriority,sys_getpriority,compat_sys_getpriority)
SYSCALL(sys_setpriority,sys_setpriority,compat_sys_setpriority)
NI_SYSCALL							/* old profil syscall */
SYSCALL(sys_statfs,sys_statfs,compat_sys_statfs)
SYSCALL(sys_fstatfs,sys_fstatfs,compat_sys_fstatfs)	/* 100 */
NI_SYSCALL							/* ioperm for i386 */
SYSCALL(sys_socketcall,sys_socketcall,compat_sys_socketcall)
SYSCALL(sys_syslog,sys_syslog,compat_sys_syslog)
SYSCALL(sys_setitimer,sys_setitimer,compat_sys_setitimer)
SYSCALL(sys_getitimer,sys_getitimer,compat_sys_getitimer)	/* 105 */
SYSCALL(sys_newstat,sys_newstat,compat_sys_newstat)
SYSCALL(sys_newlstat,sys_newlstat,compat_sys_newlstat)
SYSCALL(sys_newfstat,sys_newfstat,compat_sys_newfstat)
NI_SYSCALL							/* old uname syscall */
SYSCALL(sys_lookup_dcookie,sys_lookup_dcookie,compat_sys_lookup_dcookie)	/* 110 */
SYSCALL(sys_vhangup,sys_vhangup,sys_vhangup)
NI_SYSCALL							/* old "idle" system call */
NI_SYSCALL							/* vm86old for i386 */
SYSCALL(sys_wait4,sys_wait4,compat_sys_wait4)
SYSCALL(sys_swapoff,sys_swapoff,compat_sys_swapoff)		/* 115 */
SYSCALL(sys_sysinfo,sys_sysinfo,compat_sys_sysinfo)
SYSCALL(sys_s390_ipc,sys_s390_ipc,compat_sys_s390_ipc)
SYSCALL(sys_fsync,sys_fsync,compat_sys_fsync)
SYSCALL(sys_sigreturn,sys_sigreturn,compat_sys_sigreturn)
SYSCALL(sys_clone,sys_clone,compat_sys_clone)			/* 120 */
SYSCALL(sys_setdomainname,sys_setdomainname,compat_sys_setdomainname)
SYSCALL(sys_newuname,sys_newuname,compat_sys_newuname)
NI_SYSCALL							/* modify_ldt for i386 */
SYSCALL(sys_adjtimex,sys_adjtimex,compat_sys_adjtimex)
SYSCALL(sys_mprotect,sys_mprotect,compat_sys_mprotect)		/* 125 */
SYSCALL(sys_sigprocmask,sys_sigprocmask,compat_sys_sigprocmask)
NI_SYSCALL							/* old "create module" */
SYSCALL(sys_init_module,sys_init_module,compat_sys_init_module)
SYSCALL(sys_delete_module,sys_delete_module,compat_sys_delete_module)
NI_SYSCALL							/* 130: old get_kernel_syms */
SYSCALL(sys_quotactl,sys_quotactl,compat_sys_quotactl)
SYSCALL(sys_getpgid,sys_getpgid,compat_sys_getpgid)
SYSCALL(sys_fchdir,sys_fchdir,compat_sys_fchdir)
SYSCALL(sys_bdflush,sys_bdflush,compat_sys_bdflush)
SYSCALL(sys_sysfs,sys_sysfs,compat_sys_sysfs)		/* 135 */
SYSCALL(sys_personality,sys_s390_personality,compat_sys_s390_personality)
NI_SYSCALL							/* for afs_syscall */
SYSCALL(sys_setfsuid16,sys_ni_syscall,compat_sys_s390_setfsuid16)	/* old setfsuid16 syscall */
SYSCALL(sys_setfsgid16,sys_ni_syscall,compat_sys_s390_setfsgid16)	/* old setfsgid16 syscall */
SYSCALL(sys_llseek,sys_llseek,compat_sys_llseek)		/* 140 */
SYSCALL(sys_getdents,sys_getdents,compat_sys_getdents)
SYSCALL(sys_select,sys_select,compat_sys_select)
SYSCALL(sys_flock,sys_flock,compat_sys_flock)
SYSCALL(sys_msync,sys_msync,compat_sys_msync)
SYSCALL(sys_readv,sys_readv,compat_sys_readv)		/* 145 */
SYSCALL(sys_writev,sys_writev,compat_sys_writev)
SYSCALL(sys_getsid,sys_getsid,compat_sys_getsid)
SYSCALL(sys_fdatasync,sys_fdatasync,compat_sys_fdatasync)
SYSCALL(sys_sysctl,sys_sysctl,compat_sys_sysctl)
SYSCALL(sys_mlock,sys_mlock,compat_sys_mlock)			/* 150 */
SYSCALL(sys_munlock,sys_munlock,compat_sys_munlock)
SYSCALL(sys_mlockall,sys_mlockall,compat_sys_mlockall)
SYSCALL(sys_munlockall,sys_munlockall,sys_munlockall)
SYSCALL(sys_sched_setparam,sys_sched_setparam,compat_sys_sched_setparam)
SYSCALL(sys_sched_getparam,sys_sched_getparam,compat_sys_sched_getparam)	/* 155 */
SYSCALL(sys_sched_setscheduler,sys_sched_setscheduler,compat_sys_sched_setscheduler)
SYSCALL(sys_sched_getscheduler,sys_sched_getscheduler,compat_sys_sched_getscheduler)
SYSCALL(sys_sched_yield,sys_sched_yield,sys_sched_yield)
SYSCALL(sys_sched_get_priority_max,sys_sched_get_priority_max,compat_sys_sched_get_priority_max)
SYSCALL(sys_sched_get_priority_min,sys_sched_get_priority_min,compat_sys_sched_get_priority_min)	/* 160 */
SYSCALL(sys_sched_rr_get_interval,sys_sched_rr_get_interval,compat_sys_sched_rr_get_interval)
SYSCALL(sys_nanosleep,sys_nanosleep,compat_sys_nanosleep)
SYSCALL(sys_mremap,sys_mremap,compat_sys_mremap)
SYSCALL(sys_setresuid16,sys_ni_syscall,compat_sys_s390_setresuid16)	/* old setresuid16 syscall */
SYSCALL(sys_getresuid16,sys_ni_syscall,compat_sys_s390_getresuid16)	/* 165 old getresuid16 syscall */
NI_SYSCALL							/* for vm86 */
NI_SYSCALL							/* old sys_query_module */
SYSCALL(sys_poll,sys_poll,compat_sys_poll)
NI_SYSCALL							/* old nfsservctl */
SYSCALL(sys_setresgid16,sys_ni_syscall,compat_sys_s390_setresgid16)	/* 170 old setresgid16 syscall */
SYSCALL(sys_getresgid16,sys_ni_syscall,compat_sys_s390_getresgid16)	/* old getresgid16 syscall */
SYSCALL(sys_prctl,sys_prctl,compat_sys_prctl)
SYSCALL(sys_rt_sigreturn,sys_rt_sigreturn,compat_sys_rt_sigreturn)
SYSCALL(sys_rt_sigaction,sys_rt_sigaction,compat_sys_rt_sigaction)
SYSCALL(sys_rt_sigprocmask,sys_rt_sigprocmask,compat_sys_rt_sigprocmask) /* 175 */
SYSCALL(sys_rt_sigpending,sys_rt_sigpending,compat_sys_rt_sigpending)
SYSCALL(sys_rt_sigtimedwait,sys_rt_sigtimedwait,compat_sys_rt_sigtimedwait)
SYSCALL(sys_rt_sigqueueinfo,sys_rt_sigqueueinfo,compat_sys_rt_sigqueueinfo)
SYSCALL(sys_rt_sigsuspend,sys_rt_sigsuspend,compat_sys_rt_sigsuspend)
SYSCALL(sys_pread64,sys_pread64,compat_sys_s390_pread64)		/* 180 */
SYSCALL(sys_pwrite64,sys_pwrite64,compat_sys_s390_pwrite64)
SYSCALL(sys_chown16,sys_ni_syscall,compat_sys_s390_chown16)	/* old chown16 syscall */
SYSCALL(sys_getcwd,sys_getcwd,compat_sys_getcwd)
SYSCALL(sys_capget,sys_capget,compat_sys_capget)
SYSCALL(sys_capset,sys_capset,compat_sys_capset)		/* 185 */
SYSCALL(sys_sigaltstack,sys_sigaltstack,compat_sys_sigaltstack)
SYSCALL(sys_sendfile,sys_sendfile64,compat_sys_sendfile)
NI_SYSCALL							/* streams1 */
NI_SYSCALL							/* streams2 */
SYSCALL(sys_vfork,sys_vfork,sys_vfork)				/* 190 */
SYSCALL(sys_getrlimit,sys_getrlimit,compat_sys_getrlimit)
SYSCALL(sys_mmap2,sys_mmap2,compat_sys_s390_mmap2)
SYSCALL(sys_truncate64,sys_ni_syscall,compat_sys_s390_truncate64)
SYSCALL(sys_ftruncate64,sys_ni_syscall,compat_sys_s390_ftruncate64)
SYSCALL(sys_stat64,sys_ni_syscall,compat_sys_s390_stat64)		/* 195 */
SYSCALL(sys_lstat64,sys_ni_syscall,compat_sys_s390_lstat64)
SYSCALL(sys_fstat64,sys_ni_syscall,compat_sys_s390_fstat64)
SYSCALL(sys_lchown,sys_lchown,compat_sys_lchown)
SYSCALL(sys_getuid,sys_getuid,sys_getuid)
SYSCALL(sys_getgid,sys_getgid,sys_getgid)			/* 200 */
SYSCALL(sys_geteuid,sys_geteuid,sys_geteuid)
SYSCALL(sys_getegid,sys_getegid,sys_getegid)
SYSCALL(sys_setreuid,sys_setreuid,compat_sys_setreuid)
SYSCALL(sys_setregid,sys_setregid,compat_sys_setregid)
SYSCALL(sys_getgroups,sys_getgroups,compat_sys_getgroups)	/* 205 */
SYSCALL(sys_setgroups,sys_setgroups,compat_sys_setgroups)
SYSCALL(sys_fchown,sys_fchown,compat_sys_fchown)
SYSCALL(sys_setresuid,sys_setresuid,compat_sys_setresuid)
SYSCALL(sys_getresuid,sys_getresuid,compat_sys_getresuid)
SYSCALL(sys_setresgid,sys_setresgid,compat_sys_setresgid)	/* 210 */
SYSCALL(sys_getresgid,sys_getresgid,compat_sys_getresgid)
SYSCALL(sys_chown,sys_chown,compat_sys_chown)
SYSCALL(sys_setuid,sys_setuid,compat_sys_setuid)
SYSCALL(sys_setgid,sys_setgid,compat_sys_setgid)
SYSCALL(sys_setfsuid,sys_setfsuid,compat_sys_setfsuid)	/* 215 */
SYSCALL(sys_setfsgid,sys_setfsgid,compat_sys_setfsgid)
SYSCALL(sys_pivot_root,sys_pivot_root,compat_sys_pivot_root)
SYSCALL(sys_mincore,sys_mincore,compat_sys_mincore)
SYSCALL(sys_madvise,sys_madvise,compat_sys_madvise)
SYSCALL(sys_getdents64,sys_getdents64,compat_sys_getdents64)	/* 220 */
SYSCALL(sys_fcntl64,sys_ni_syscall,compat_sys_fcntl64)
SYSCALL(sys_readahead,sys_readahead,compat_sys_s390_readahead)
SYSCALL(sys_sendfile64,sys_ni_syscall,compat_sys_sendfile64)
SYSCALL(sys_setxattr,sys_setxattr,compat_sys_setxattr)
SYSCALL(sys_lsetxattr,sys_lsetxattr,compat_sys_lsetxattr)	/* 225 */
SYSCALL(sys_fsetxattr,sys_fsetxattr,compat_sys_fsetxattr)
SYSCALL(sys_getxattr,sys_getxattr,compat_sys_getxattr)
SYSCALL(sys_lgetxattr,sys_lgetxattr,compat_sys_lgetxattr)
SYSCALL(sys_fgetxattr,sys_fgetxattr,compat_sys_fgetxattr)
SYSCALL(sys_listxattr,sys_listxattr,compat_sys_listxattr)	/* 230 */
SYSCALL(sys_llistxattr,sys_llistxattr,compat_sys_llistxattr)
SYSCALL(sys_flistxattr,sys_flistxattr,compat_sys_flistxattr)
SYSCALL(sys_removexattr,sys_removexattr,compat_sys_removexattr)
SYSCALL(sys_lremovexattr,sys_lremovexattr,compat_sys_lremovexattr)
SYSCALL(sys_fremovexattr,sys_fremovexattr,compat_sys_fremovexattr)	/* 235 */
SYSCALL(sys_gettid,sys_gettid,sys_gettid)
SYSCALL(sys_tkill,sys_tkill,compat_sys_tkill)
SYSCALL(sys_futex,sys_futex,compat_sys_futex)
SYSCALL(sys_sched_setaffinity,sys_sched_setaffinity,compat_sys_sched_setaffinity)
SYSCALL(sys_sched_getaffinity,sys_sched_getaffinity,compat_sys_sched_getaffinity)	/* 240 */
SYSCALL(sys_tgkill,sys_tgkill,compat_sys_tgkill)
NI_SYSCALL							/* reserved for TUX */
SYSCALL(sys_io_setup,sys_io_setup,compat_sys_io_setup)
SYSCALL(sys_io_destroy,sys_io_destroy,compat_sys_io_destroy)
SYSCALL(sys_io_getevents,sys_io_getevents,compat_sys_io_getevents)	/* 245 */
SYSCALL(sys_io_submit,sys_io_submit,compat_sys_io_submit)
SYSCALL(sys_io_cancel,sys_io_cancel,compat_sys_io_cancel)
SYSCALL(sys_exit_group,sys_exit_group,compat_sys_exit_group)
SYSCALL(sys_epoll_create,sys_epoll_create,compat_sys_epoll_create)
SYSCALL(sys_epoll_ctl,sys_epoll_ctl,compat_sys_epoll_ctl)	/* 250 */
SYSCALL(sys_epoll_wait,sys_epoll_wait,compat_sys_epoll_wait)
SYSCALL(sys_set_tid_address,sys_set_tid_address,compat_sys_set_tid_address)
SYSCALL(sys_s390_fadvise64,sys_fadvise64_64,compat_sys_s390_fadvise64)
SYSCALL(sys_timer_create,sys_timer_create,compat_sys_timer_create)
SYSCALL(sys_timer_settime,sys_timer_settime,compat_sys_timer_settime)	/* 255 */
SYSCALL(sys_timer_gettime,sys_timer_gettime,compat_sys_timer_gettime)
SYSCALL(sys_timer_getoverrun,sys_timer_getoverrun,compat_sys_timer_getoverrun)
SYSCALL(sys_timer_delete,sys_timer_delete,compat_sys_timer_delete)
SYSCALL(sys_clock_settime,sys_clock_settime,compat_sys_clock_settime)
SYSCALL(sys_clock_gettime,sys_clock_gettime,compat_sys_clock_gettime)	/* 260 */
SYSCALL(sys_clock_getres,sys_clock_getres,compat_sys_clock_getres)
SYSCALL(sys_clock_nanosleep,sys_clock_nanosleep,compat_sys_clock_nanosleep)
NI_SYSCALL							/* reserved for vserver */
SYSCALL(sys_s390_fadvise64_64,sys_ni_syscall,compat_sys_s390_fadvise64_64)
SYSCALL(sys_statfs64,sys_statfs64,compat_sys_statfs64)
SYSCALL(sys_fstatfs64,sys_fstatfs64,compat_sys_fstatfs64)
SYSCALL(sys_remap_file_pages,sys_remap_file_pages,compat_sys_remap_file_pages)
NI_SYSCALL							/* 268 sys_mbind */
NI_SYSCALL							/* 269 sys_get_mempolicy */
NI_SYSCALL							/* 270 sys_set_mempolicy */
SYSCALL(sys_mq_open,sys_mq_open,compat_sys_mq_open)
SYSCALL(sys_mq_unlink,sys_mq_unlink,compat_sys_mq_unlink)
SYSCALL(sys_mq_timedsend,sys_mq_timedsend,compat_sys_mq_timedsend)
SYSCALL(sys_mq_timedreceive,sys_mq_timedreceive,compat_sys_mq_timedreceive)
SYSCALL(sys_mq_notify,sys_mq_notify,compat_sys_mq_notify) /* 275 */
SYSCALL(sys_mq_getsetattr,sys_mq_getsetattr,compat_sys_mq_getsetattr)
SYSCALL(sys_kexec_load,sys_kexec_load,compat_sys_kexec_load)
SYSCALL(sys_add_key,sys_add_key,compat_sys_add_key)
SYSCALL(sys_request_key,sys_request_key,compat_sys_request_key)
SYSCALL(sys_keyctl,sys_keyctl,compat_sys_keyctl)		/* 280 */
SYSCALL(sys_waitid,sys_waitid,compat_sys_waitid)
SYSCALL(sys_ioprio_set,sys_ioprio_set,compat_sys_ioprio_set)
SYSCALL(sys_ioprio_get,sys_ioprio_get,compat_sys_ioprio_get)
SYSCALL(sys_inotify_init,sys_inotify_init,sys_inotify_init)
SYSCALL(sys_inotify_add_watch,sys_inotify_add_watch,compat_sys_inotify_add_watch)	/* 285 */
SYSCALL(sys_inotify_rm_watch,sys_inotify_rm_watch,compat_sys_inotify_rm_watch)
NI_SYSCALL							/* 287 sys_migrate_pages */
SYSCALL(sys_openat,sys_openat,compat_sys_openat)
SYSCALL(sys_mkdirat,sys_mkdirat,compat_sys_mkdirat)
SYSCALL(sys_mknodat,sys_mknodat,compat_sys_mknodat)	/* 290 */
SYSCALL(sys_fchownat,sys_fchownat,compat_sys_fchownat)
SYSCALL(sys_futimesat,sys_futimesat,compat_sys_futimesat)
SYSCALL(sys_fstatat64,sys_newfstatat,compat_sys_s390_fstatat64)
SYSCALL(sys_unlinkat,sys_unlinkat,compat_sys_unlinkat)
SYSCALL(sys_renameat,sys_renameat,compat_sys_renameat)	/* 295 */
SYSCALL(sys_linkat,sys_linkat,compat_sys_linkat)
SYSCALL(sys_symlinkat,sys_symlinkat,compat_sys_symlinkat)
SYSCALL(sys_readlinkat,sys_readlinkat,compat_sys_readlinkat)
SYSCALL(sys_fchmodat,sys_fchmodat,compat_sys_fchmodat)
SYSCALL(sys_faccessat,sys_faccessat,compat_sys_faccessat)	/* 300 */
SYSCALL(sys_pselect6,sys_pselect6,compat_sys_pselect6)
SYSCALL(sys_ppoll,sys_ppoll,compat_sys_ppoll)
SYSCALL(sys_unshare,sys_unshare,compat_sys_unshare)
SYSCALL(sys_set_robust_list,sys_set_robust_list,compat_sys_set_robust_list)
SYSCALL(sys_get_robust_list,sys_get_robust_list,compat_sys_get_robust_list)
SYSCALL(sys_splice,sys_splice,compat_sys_splice)
SYSCALL(sys_sync_file_range,sys_sync_file_range,compat_sys_s390_sync_file_range)
SYSCALL(sys_tee,sys_tee,compat_sys_tee)
SYSCALL(sys_vmsplice,sys_vmsplice,compat_sys_vmsplice)
NI_SYSCALL							/* 310 sys_move_pages */
SYSCALL(sys_getcpu,sys_getcpu,compat_sys_getcpu)
SYSCALL(sys_epoll_pwait,sys_epoll_pwait,compat_sys_epoll_pwait)
SYSCALL(sys_utimes,sys_utimes,compat_sys_utimes)
SYSCALL(sys_s390_fallocate,sys_fallocate,compat_sys_s390_fallocate)
SYSCALL(sys_utimensat,sys_utimensat,compat_sys_utimensat)	/* 315 */
SYSCALL(sys_signalfd,sys_signalfd,compat_sys_signalfd)
NI_SYSCALL						/* 317 old sys_timer_fd */
SYSCALL(sys_eventfd,sys_eventfd,compat_sys_eventfd)
SYSCALL(sys_timerfd_create,sys_timerfd_create,compat_sys_timerfd_create)
SYSCALL(sys_timerfd_settime,sys_timerfd_settime,compat_sys_timerfd_settime) /* 320 */
SYSCALL(sys_timerfd_gettime,sys_timerfd_gettime,compat_sys_timerfd_gettime)
SYSCALL(sys_signalfd4,sys_signalfd4,compat_sys_signalfd4)
SYSCALL(sys_eventfd2,sys_eventfd2,compat_sys_eventfd2)
SYSCALL(sys_inotify_init1,sys_inotify_init1,compat_sys_inotify_init1)
SYSCALL(sys_pipe2,sys_pipe2,compat_sys_pipe2) /* 325 */
SYSCALL(sys_dup3,sys_dup3,compat_sys_dup3)
SYSCALL(sys_epoll_create1,sys_epoll_create1,compat_sys_epoll_create1)
SYSCALL(sys_preadv,sys_preadv,compat_sys_preadv)
SYSCALL(sys_pwritev,sys_pwritev,compat_sys_pwritev)
SYSCALL(sys_rt_tgsigqueueinfo,sys_rt_tgsigqueueinfo,compat_sys_rt_tgsigqueueinfo) /* 330 */
SYSCALL(sys_perf_event_open,sys_perf_event_open,compat_sys_perf_event_open)
SYSCALL(sys_fanotify_init,sys_fanotify_init,compat_sys_fanotify_init)
SYSCALL(sys_fanotify_mark,sys_fanotify_mark,compat_sys_fanotify_mark)
SYSCALL(sys_prlimit64,sys_prlimit64,compat_sys_prlimit64)
SYSCALL(sys_name_to_handle_at,sys_name_to_handle_at,compat_sys_name_to_handle_at) /* 335 */
SYSCALL(sys_open_by_handle_at,sys_open_by_handle_at,compat_sys_open_by_handle_at)
SYSCALL(sys_clock_adjtime,sys_clock_adjtime,compat_sys_clock_adjtime)
SYSCALL(sys_syncfs,sys_syncfs,compat_sys_syncfs)
SYSCALL(sys_setns,sys_setns,compat_sys_setns)
SYSCALL(sys_process_vm_readv,sys_process_vm_readv,compat_sys_process_vm_readv) /* 340 */
SYSCALL(sys_process_vm_writev,sys_process_vm_writev,compat_sys_process_vm_writev)
SYSCALL(sys_ni_syscall,sys_s390_runtime_instr,compat_sys_s390_runtime_instr)
SYSCALL(sys_kcmp,sys_kcmp,compat_sys_kcmp)
SYSCALL(sys_finit_module,sys_finit_module,compat_sys_finit_module)
SYSCALL(sys_sched_setattr,sys_sched_setattr,compat_sys_sched_setattr) /* 345 */
SYSCALL(sys_sched_getattr,sys_sched_getattr,compat_sys_sched_getattr)
SYSCALL(sys_renameat2,sys_renameat2,compat_sys_renameat2)
SYSCALL(sys_seccomp,sys_seccomp,compat_sys_seccomp)
SYSCALL(sys_getrandom,sys_getrandom,compat_sys_getrandom)
SYSCALL(sys_memfd_create,sys_memfd_create,compat_sys_memfd_create) /* 350 */
SYSCALL(sys_bpf,sys_bpf,compat_sys_bpf)
