/*
 * definitions for sys_call_table, each line represents an
 * entry in the table in the form 
 * SYSCALL(31 bit syscall, 64 bit syscall, 31 bit emulated syscall)
 *
 * this file is meant to be included from entry.S and entry64.S
 */

#define NI_SYSCALL SYSCALL(sys_ni_syscall,sys_ni_syscall,sys_ni_syscall)

NI_SYSCALL							/* 0 */
SYSCALL(sys_exit,sys_exit,sys32_exit_wrapper)
SYSCALL(sys_fork,sys_fork,sys_fork)
SYSCALL(sys_read,sys_read,sys32_read_wrapper)
SYSCALL(sys_write,sys_write,sys32_write_wrapper)
SYSCALL(sys_open,sys_open,sys32_open_wrapper)			/* 5 */
SYSCALL(sys_close,sys_close,sys32_close_wrapper)
SYSCALL(sys_restart_syscall,sys_restart_syscall,sys_restart_syscall)
SYSCALL(sys_creat,sys_creat,sys32_creat_wrapper)
SYSCALL(sys_link,sys_link,sys32_link_wrapper)
SYSCALL(sys_unlink,sys_unlink,sys32_unlink_wrapper)		/* 10 */
SYSCALL(sys_execve,sys_execve,sys32_execve_wrapper)
SYSCALL(sys_chdir,sys_chdir,sys32_chdir_wrapper)
SYSCALL(sys_time,sys_ni_syscall,sys32_time_wrapper)		/* old time syscall */
SYSCALL(sys_mknod,sys_mknod,sys32_mknod_wrapper)
SYSCALL(sys_chmod,sys_chmod,sys32_chmod_wrapper)		/* 15 */
SYSCALL(sys_lchown16,sys_ni_syscall,sys32_lchown16_wrapper)	/* old lchown16 syscall*/
NI_SYSCALL							/* old break syscall holder */
NI_SYSCALL							/* old stat syscall holder */
SYSCALL(sys_lseek,sys_lseek,sys32_lseek_wrapper)
SYSCALL(sys_getpid,sys_getpid,sys_getpid)			/* 20 */
SYSCALL(sys_mount,sys_mount,sys32_mount_wrapper)
SYSCALL(sys_oldumount,sys_oldumount,sys32_oldumount_wrapper)
SYSCALL(sys_setuid16,sys_ni_syscall,sys32_setuid16_wrapper)	/* old setuid16 syscall*/
SYSCALL(sys_getuid16,sys_ni_syscall,sys32_getuid16)		/* old getuid16 syscall*/
SYSCALL(sys_stime,sys_ni_syscall,sys32_stime_wrapper)		/* 25 old stime syscall */
SYSCALL(sys_ptrace,sys_ptrace,sys32_ptrace_wrapper)
SYSCALL(sys_alarm,sys_alarm,sys32_alarm_wrapper)
NI_SYSCALL							/* old fstat syscall */
SYSCALL(sys_pause,sys_pause,sys_pause)
SYSCALL(sys_utime,sys_utime,compat_sys_utime_wrapper)		/* 30 */
NI_SYSCALL							/* old stty syscall */
NI_SYSCALL							/* old gtty syscall */
SYSCALL(sys_access,sys_access,sys32_access_wrapper)
SYSCALL(sys_nice,sys_nice,sys32_nice_wrapper)
NI_SYSCALL							/* 35 old ftime syscall */
SYSCALL(sys_sync,sys_sync,sys_sync)
SYSCALL(sys_kill,sys_kill,sys32_kill_wrapper)
SYSCALL(sys_rename,sys_rename,sys32_rename_wrapper)
SYSCALL(sys_mkdir,sys_mkdir,sys32_mkdir_wrapper)
SYSCALL(sys_rmdir,sys_rmdir,sys32_rmdir_wrapper)		/* 40 */
SYSCALL(sys_dup,sys_dup,sys32_dup_wrapper)
SYSCALL(sys_pipe,sys_pipe,sys32_pipe_wrapper)
SYSCALL(sys_times,sys_times,compat_sys_times_wrapper)
NI_SYSCALL							/* old prof syscall */
SYSCALL(sys_brk,sys_brk,sys32_brk_wrapper)			/* 45 */
SYSCALL(sys_setgid16,sys_ni_syscall,sys32_setgid16_wrapper)	/* old setgid16 syscall*/
SYSCALL(sys_getgid16,sys_ni_syscall,sys32_getgid16)		/* old getgid16 syscall*/
SYSCALL(sys_signal,sys_signal,sys32_signal_wrapper)
SYSCALL(sys_geteuid16,sys_ni_syscall,sys32_geteuid16)		/* old geteuid16 syscall */
SYSCALL(sys_getegid16,sys_ni_syscall,sys32_getegid16)		/* 50 old getegid16 syscall */
SYSCALL(sys_acct,sys_acct,sys32_acct_wrapper)
SYSCALL(sys_umount,sys_umount,sys32_umount_wrapper)
NI_SYSCALL							/* old lock syscall */
SYSCALL(sys_ioctl,sys_ioctl,compat_sys_ioctl_wrapper)
SYSCALL(sys_fcntl,sys_fcntl,compat_sys_fcntl_wrapper)		/* 55 */
NI_SYSCALL							/* intel mpx syscall */
SYSCALL(sys_setpgid,sys_setpgid,sys32_setpgid_wrapper)
NI_SYSCALL							/* old ulimit syscall */
NI_SYSCALL							/* old uname syscall */
SYSCALL(sys_umask,sys_umask,sys32_umask_wrapper)		/* 60 */
SYSCALL(sys_chroot,sys_chroot,sys32_chroot_wrapper)
SYSCALL(sys_ustat,sys_ustat,sys32_ustat_wrapper)
SYSCALL(sys_dup2,sys_dup2,sys32_dup2_wrapper)
SYSCALL(sys_getppid,sys_getppid,sys_getppid)
SYSCALL(sys_getpgrp,sys_getpgrp,sys_getpgrp)			/* 65 */
SYSCALL(sys_setsid,sys_setsid,sys_setsid)
SYSCALL(sys_sigaction,sys_sigaction,sys32_sigaction_wrapper)
NI_SYSCALL							/* old sgetmask syscall*/
NI_SYSCALL							/* old ssetmask syscall*/
SYSCALL(sys_setreuid16,sys_ni_syscall,sys32_setreuid16_wrapper)	/* old setreuid16 syscall */
SYSCALL(sys_setregid16,sys_ni_syscall,sys32_setregid16_wrapper)	/* old setregid16 syscall */
SYSCALL(sys_sigsuspend,sys_sigsuspend,sys_sigsuspend_wrapper)
SYSCALL(sys_sigpending,sys_sigpending,compat_sys_sigpending_wrapper)
SYSCALL(sys_sethostname,sys_sethostname,sys32_sethostname_wrapper)
SYSCALL(sys_setrlimit,sys_setrlimit,compat_sys_setrlimit_wrapper)	/* 75 */
SYSCALL(sys_old_getrlimit,sys_getrlimit,compat_sys_old_getrlimit_wrapper)
SYSCALL(sys_getrusage,sys_getrusage,compat_sys_getrusage_wrapper)
SYSCALL(sys_gettimeofday,sys_gettimeofday,compat_sys_gettimeofday_wrapper)
SYSCALL(sys_settimeofday,sys_settimeofday,compat_sys_settimeofday_wrapper)
SYSCALL(sys_getgroups16,sys_ni_syscall,sys32_getgroups16_wrapper)	/* 80 old getgroups16 syscall */
SYSCALL(sys_setgroups16,sys_ni_syscall,sys32_setgroups16_wrapper)	/* old setgroups16 syscall */
NI_SYSCALL							/* old select syscall */
SYSCALL(sys_symlink,sys_symlink,sys32_symlink_wrapper)
NI_SYSCALL							/* old lstat syscall */
SYSCALL(sys_readlink,sys_readlink,sys32_readlink_wrapper)	/* 85 */
SYSCALL(sys_uselib,sys_uselib,sys32_uselib_wrapper)
SYSCALL(sys_swapon,sys_swapon,sys32_swapon_wrapper)
SYSCALL(sys_reboot,sys_reboot,sys32_reboot_wrapper)
SYSCALL(sys_ni_syscall,sys_ni_syscall,old32_readdir_wrapper)	/* old readdir syscall */
SYSCALL(sys_old_mmap,sys_old_mmap,old32_mmap_wrapper)		/* 90 */
SYSCALL(sys_munmap,sys_munmap,sys32_munmap_wrapper)
SYSCALL(sys_truncate,sys_truncate,sys32_truncate_wrapper)
SYSCALL(sys_ftruncate,sys_ftruncate,sys32_ftruncate_wrapper)
SYSCALL(sys_fchmod,sys_fchmod,sys32_fchmod_wrapper)
SYSCALL(sys_fchown16,sys_ni_syscall,sys32_fchown16_wrapper)	/* 95 old fchown16 syscall*/
SYSCALL(sys_getpriority,sys_getpriority,sys32_getpriority_wrapper)
SYSCALL(sys_setpriority,sys_setpriority,sys32_setpriority_wrapper)
NI_SYSCALL							/* old profil syscall */
SYSCALL(sys_statfs,sys_statfs,compat_sys_statfs_wrapper)
SYSCALL(sys_fstatfs,sys_fstatfs,compat_sys_fstatfs_wrapper)	/* 100 */
NI_SYSCALL							/* ioperm for i386 */
SYSCALL(sys_socketcall,sys_socketcall,compat_sys_socketcall_wrapper)
SYSCALL(sys_syslog,sys_syslog,sys32_syslog_wrapper)
SYSCALL(sys_setitimer,sys_setitimer,compat_sys_setitimer_wrapper)
SYSCALL(sys_getitimer,sys_getitimer,compat_sys_getitimer_wrapper)	/* 105 */
SYSCALL(sys_newstat,sys_newstat,compat_sys_newstat_wrapper)
SYSCALL(sys_newlstat,sys_newlstat,compat_sys_newlstat_wrapper)
SYSCALL(sys_newfstat,sys_newfstat,compat_sys_newfstat_wrapper)
NI_SYSCALL							/* old uname syscall */
SYSCALL(sys_lookup_dcookie,sys_lookup_dcookie,sys32_lookup_dcookie_wrapper)	/* 110 */
SYSCALL(sys_vhangup,sys_vhangup,sys_vhangup)
NI_SYSCALL							/* old "idle" system call */
NI_SYSCALL							/* vm86old for i386 */
SYSCALL(sys_wait4,sys_wait4,compat_sys_wait4_wrapper)
SYSCALL(sys_swapoff,sys_swapoff,sys32_swapoff_wrapper)		/* 115 */
SYSCALL(sys_sysinfo,sys_sysinfo,compat_sys_sysinfo_wrapper)
SYSCALL(sys_s390_ipc,sys_s390_ipc,sys32_ipc_wrapper)
SYSCALL(sys_fsync,sys_fsync,sys32_fsync_wrapper)
SYSCALL(sys_sigreturn,sys_sigreturn,sys32_sigreturn)
SYSCALL(sys_clone,sys_clone,sys_clone_wrapper)			/* 120 */
SYSCALL(sys_setdomainname,sys_setdomainname,sys32_setdomainname_wrapper)
SYSCALL(sys_newuname,sys_newuname,sys32_newuname_wrapper)
NI_SYSCALL							/* modify_ldt for i386 */
SYSCALL(sys_adjtimex,sys_adjtimex,compat_sys_adjtimex_wrapper)
SYSCALL(sys_mprotect,sys_mprotect,sys32_mprotect_wrapper)	/* 125 */
SYSCALL(sys_sigprocmask,sys_sigprocmask,compat_sys_sigprocmask_wrapper)
NI_SYSCALL							/* old "create module" */
SYSCALL(sys_init_module,sys_init_module,sys_init_module_wrapper)
SYSCALL(sys_delete_module,sys_delete_module,sys_delete_module_wrapper)
NI_SYSCALL							/* 130: old get_kernel_syms */
SYSCALL(sys_quotactl,sys_quotactl,sys32_quotactl_wrapper)
SYSCALL(sys_getpgid,sys_getpgid,sys32_getpgid_wrapper)
SYSCALL(sys_fchdir,sys_fchdir,sys32_fchdir_wrapper)
SYSCALL(sys_bdflush,sys_bdflush,sys32_bdflush_wrapper)
SYSCALL(sys_sysfs,sys_sysfs,sys32_sysfs_wrapper)		/* 135 */
SYSCALL(sys_personality,sys_s390_personality,sys32_personality_wrapper)
NI_SYSCALL							/* for afs_syscall */
SYSCALL(sys_setfsuid16,sys_ni_syscall,sys32_setfsuid16_wrapper)	/* old setfsuid16 syscall */
SYSCALL(sys_setfsgid16,sys_ni_syscall,sys32_setfsgid16_wrapper)	/* old setfsgid16 syscall */
SYSCALL(sys_llseek,sys_llseek,sys32_llseek_wrapper)		/* 140 */
SYSCALL(sys_getdents,sys_getdents,sys32_getdents_wrapper)
SYSCALL(sys_select,sys_select,compat_sys_select_wrapper)
SYSCALL(sys_flock,sys_flock,sys32_flock_wrapper)
SYSCALL(sys_msync,sys_msync,sys32_msync_wrapper)
SYSCALL(sys_readv,sys_readv,compat_sys_readv_wrapper)		/* 145 */
SYSCALL(sys_writev,sys_writev,compat_sys_writev_wrapper)
SYSCALL(sys_getsid,sys_getsid,sys32_getsid_wrapper)
SYSCALL(sys_fdatasync,sys_fdatasync,sys32_fdatasync_wrapper)
SYSCALL(sys_sysctl,sys_sysctl,sys32_sysctl_wrapper)
SYSCALL(sys_mlock,sys_mlock,sys32_mlock_wrapper)		/* 150 */
SYSCALL(sys_munlock,sys_munlock,sys32_munlock_wrapper)
SYSCALL(sys_mlockall,sys_mlockall,sys32_mlockall_wrapper)
SYSCALL(sys_munlockall,sys_munlockall,sys_munlockall)
SYSCALL(sys_sched_setparam,sys_sched_setparam,sys32_sched_setparam_wrapper)
SYSCALL(sys_sched_getparam,sys_sched_getparam,sys32_sched_getparam_wrapper)	/* 155 */
SYSCALL(sys_sched_setscheduler,sys_sched_setscheduler,sys32_sched_setscheduler_wrapper)
SYSCALL(sys_sched_getscheduler,sys_sched_getscheduler,sys32_sched_getscheduler_wrapper)
SYSCALL(sys_sched_yield,sys_sched_yield,sys_sched_yield)
SYSCALL(sys_sched_get_priority_max,sys_sched_get_priority_max,sys32_sched_get_priority_max_wrapper)
SYSCALL(sys_sched_get_priority_min,sys_sched_get_priority_min,sys32_sched_get_priority_min_wrapper)	/* 160 */
SYSCALL(sys_sched_rr_get_interval,sys_sched_rr_get_interval,sys32_sched_rr_get_interval_wrapper)
SYSCALL(sys_nanosleep,sys_nanosleep,compat_sys_nanosleep_wrapper)
SYSCALL(sys_mremap,sys_mremap,sys32_mremap_wrapper)
SYSCALL(sys_setresuid16,sys_ni_syscall,sys32_setresuid16_wrapper)	/* old setresuid16 syscall */
SYSCALL(sys_getresuid16,sys_ni_syscall,sys32_getresuid16_wrapper)	/* 165 old getresuid16 syscall */
NI_SYSCALL							/* for vm86 */
NI_SYSCALL							/* old sys_query_module */
SYSCALL(sys_poll,sys_poll,sys32_poll_wrapper)
NI_SYSCALL							/* old nfsservctl */
SYSCALL(sys_setresgid16,sys_ni_syscall,sys32_setresgid16_wrapper)	/* 170 old setresgid16 syscall */
SYSCALL(sys_getresgid16,sys_ni_syscall,sys32_getresgid16_wrapper)	/* old getresgid16 syscall */
SYSCALL(sys_prctl,sys_prctl,sys32_prctl_wrapper)
SYSCALL(sys_rt_sigreturn,sys_rt_sigreturn,sys32_rt_sigreturn)
SYSCALL(sys_rt_sigaction,sys_rt_sigaction,sys32_rt_sigaction_wrapper)
SYSCALL(sys_rt_sigprocmask,sys_rt_sigprocmask,sys32_rt_sigprocmask_wrapper)	/* 175 */
SYSCALL(sys_rt_sigpending,sys_rt_sigpending,sys32_rt_sigpending_wrapper)
SYSCALL(sys_rt_sigtimedwait,sys_rt_sigtimedwait,compat_sys_rt_sigtimedwait_wrapper)
SYSCALL(sys_rt_sigqueueinfo,sys_rt_sigqueueinfo,sys32_rt_sigqueueinfo_wrapper)
SYSCALL(sys_rt_sigsuspend,sys_rt_sigsuspend,compat_sys_rt_sigsuspend_wrapper)
SYSCALL(sys_pread64,sys_pread64,sys32_pread64_wrapper)		/* 180 */
SYSCALL(sys_pwrite64,sys_pwrite64,sys32_pwrite64_wrapper)
SYSCALL(sys_chown16,sys_ni_syscall,sys32_chown16_wrapper)	/* old chown16 syscall */
SYSCALL(sys_getcwd,sys_getcwd,sys32_getcwd_wrapper)
SYSCALL(sys_capget,sys_capget,sys32_capget_wrapper)
SYSCALL(sys_capset,sys_capset,sys32_capset_wrapper)		/* 185 */
SYSCALL(sys_sigaltstack,sys_sigaltstack,sys32_sigaltstack_wrapper)
SYSCALL(sys_sendfile,sys_sendfile64,sys32_sendfile_wrapper)
NI_SYSCALL							/* streams1 */
NI_SYSCALL							/* streams2 */
SYSCALL(sys_vfork,sys_vfork,sys_vfork)				/* 190 */
SYSCALL(sys_getrlimit,sys_getrlimit,compat_sys_getrlimit_wrapper)
SYSCALL(sys_mmap2,sys_mmap2,sys32_mmap2_wrapper)
SYSCALL(sys_truncate64,sys_ni_syscall,sys32_truncate64_wrapper)
SYSCALL(sys_ftruncate64,sys_ni_syscall,sys32_ftruncate64_wrapper)
SYSCALL(sys_stat64,sys_ni_syscall,sys32_stat64_wrapper)		/* 195 */
SYSCALL(sys_lstat64,sys_ni_syscall,sys32_lstat64_wrapper)
SYSCALL(sys_fstat64,sys_ni_syscall,sys32_fstat64_wrapper)
SYSCALL(sys_lchown,sys_lchown,sys32_lchown_wrapper)
SYSCALL(sys_getuid,sys_getuid,sys_getuid)
SYSCALL(sys_getgid,sys_getgid,sys_getgid)			/* 200 */
SYSCALL(sys_geteuid,sys_geteuid,sys_geteuid)
SYSCALL(sys_getegid,sys_getegid,sys_getegid)
SYSCALL(sys_setreuid,sys_setreuid,sys32_setreuid_wrapper)
SYSCALL(sys_setregid,sys_setregid,sys32_setregid_wrapper)
SYSCALL(sys_getgroups,sys_getgroups,sys32_getgroups_wrapper)	/* 205 */
SYSCALL(sys_setgroups,sys_setgroups,sys32_setgroups_wrapper)
SYSCALL(sys_fchown,sys_fchown,sys32_fchown_wrapper)
SYSCALL(sys_setresuid,sys_setresuid,sys32_setresuid_wrapper)
SYSCALL(sys_getresuid,sys_getresuid,sys32_getresuid_wrapper)
SYSCALL(sys_setresgid,sys_setresgid,sys32_setresgid_wrapper)	/* 210 */
SYSCALL(sys_getresgid,sys_getresgid,sys32_getresgid_wrapper)
SYSCALL(sys_chown,sys_chown,sys32_chown_wrapper)
SYSCALL(sys_setuid,sys_setuid,sys32_setuid_wrapper)
SYSCALL(sys_setgid,sys_setgid,sys32_setgid_wrapper)
SYSCALL(sys_setfsuid,sys_setfsuid,sys32_setfsuid_wrapper)	/* 215 */
SYSCALL(sys_setfsgid,sys_setfsgid,sys32_setfsgid_wrapper)
SYSCALL(sys_pivot_root,sys_pivot_root,sys32_pivot_root_wrapper)
SYSCALL(sys_mincore,sys_mincore,sys32_mincore_wrapper)
SYSCALL(sys_madvise,sys_madvise,sys32_madvise_wrapper)
SYSCALL(sys_getdents64,sys_getdents64,sys32_getdents64_wrapper)	/* 220 */
SYSCALL(sys_fcntl64,sys_ni_syscall,compat_sys_fcntl64_wrapper)
SYSCALL(sys_readahead,sys_readahead,sys32_readahead_wrapper)
SYSCALL(sys_sendfile64,sys_ni_syscall,sys32_sendfile64_wrapper)
SYSCALL(sys_setxattr,sys_setxattr,sys32_setxattr_wrapper)
SYSCALL(sys_lsetxattr,sys_lsetxattr,sys32_lsetxattr_wrapper)	/* 225 */
SYSCALL(sys_fsetxattr,sys_fsetxattr,sys32_fsetxattr_wrapper)
SYSCALL(sys_getxattr,sys_getxattr,sys32_getxattr_wrapper)
SYSCALL(sys_lgetxattr,sys_lgetxattr,sys32_lgetxattr_wrapper)
SYSCALL(sys_fgetxattr,sys_fgetxattr,sys32_fgetxattr_wrapper)
SYSCALL(sys_listxattr,sys_listxattr,sys32_listxattr_wrapper)	/* 230 */
SYSCALL(sys_llistxattr,sys_llistxattr,sys32_llistxattr_wrapper)
SYSCALL(sys_flistxattr,sys_flistxattr,sys32_flistxattr_wrapper)
SYSCALL(sys_removexattr,sys_removexattr,sys32_removexattr_wrapper)
SYSCALL(sys_lremovexattr,sys_lremovexattr,sys32_lremovexattr_wrapper)
SYSCALL(sys_fremovexattr,sys_fremovexattr,sys32_fremovexattr_wrapper)	/* 235 */
SYSCALL(sys_gettid,sys_gettid,sys_gettid)
SYSCALL(sys_tkill,sys_tkill,sys_tkill_wrapper)
SYSCALL(sys_futex,sys_futex,compat_sys_futex_wrapper)
SYSCALL(sys_sched_setaffinity,sys_sched_setaffinity,sys32_sched_setaffinity_wrapper)
SYSCALL(sys_sched_getaffinity,sys_sched_getaffinity,sys32_sched_getaffinity_wrapper)	/* 240 */
SYSCALL(sys_tgkill,sys_tgkill,sys_tgkill_wrapper)
NI_SYSCALL							/* reserved for TUX */
SYSCALL(sys_io_setup,sys_io_setup,sys32_io_setup_wrapper)
SYSCALL(sys_io_destroy,sys_io_destroy,sys32_io_destroy_wrapper)
SYSCALL(sys_io_getevents,sys_io_getevents,sys32_io_getevents_wrapper)	/* 245 */
SYSCALL(sys_io_submit,sys_io_submit,sys32_io_submit_wrapper)
SYSCALL(sys_io_cancel,sys_io_cancel,sys32_io_cancel_wrapper)
SYSCALL(sys_exit_group,sys_exit_group,sys32_exit_group_wrapper)
SYSCALL(sys_epoll_create,sys_epoll_create,sys_epoll_create_wrapper)
SYSCALL(sys_epoll_ctl,sys_epoll_ctl,sys_epoll_ctl_wrapper)	/* 250 */
SYSCALL(sys_epoll_wait,sys_epoll_wait,sys_epoll_wait_wrapper)
SYSCALL(sys_set_tid_address,sys_set_tid_address,sys32_set_tid_address_wrapper)
SYSCALL(sys_s390_fadvise64,sys_fadvise64_64,sys32_fadvise64_wrapper)
SYSCALL(sys_timer_create,sys_timer_create,sys32_timer_create_wrapper)
SYSCALL(sys_timer_settime,sys_timer_settime,sys32_timer_settime_wrapper)	/* 255 */
SYSCALL(sys_timer_gettime,sys_timer_gettime,sys32_timer_gettime_wrapper)
SYSCALL(sys_timer_getoverrun,sys_timer_getoverrun,sys32_timer_getoverrun_wrapper)
SYSCALL(sys_timer_delete,sys_timer_delete,sys32_timer_delete_wrapper)
SYSCALL(sys_clock_settime,sys_clock_settime,sys32_clock_settime_wrapper)
SYSCALL(sys_clock_gettime,sys_clock_gettime,sys32_clock_gettime_wrapper)	/* 260 */
SYSCALL(sys_clock_getres,sys_clock_getres,sys32_clock_getres_wrapper)
SYSCALL(sys_clock_nanosleep,sys_clock_nanosleep,sys32_clock_nanosleep_wrapper)
NI_SYSCALL							/* reserved for vserver */
SYSCALL(sys_s390_fadvise64_64,sys_ni_syscall,sys32_fadvise64_64_wrapper)
SYSCALL(sys_statfs64,sys_statfs64,compat_sys_statfs64_wrapper)
SYSCALL(sys_fstatfs64,sys_fstatfs64,compat_sys_fstatfs64_wrapper)
SYSCALL(sys_remap_file_pages,sys_remap_file_pages,sys32_remap_file_pages_wrapper)
NI_SYSCALL							/* 268 sys_mbind */
NI_SYSCALL							/* 269 sys_get_mempolicy */
NI_SYSCALL							/* 270 sys_set_mempolicy */
SYSCALL(sys_mq_open,sys_mq_open,compat_sys_mq_open_wrapper)
SYSCALL(sys_mq_unlink,sys_mq_unlink,sys32_mq_unlink_wrapper)
SYSCALL(sys_mq_timedsend,sys_mq_timedsend,compat_sys_mq_timedsend_wrapper)
SYSCALL(sys_mq_timedreceive,sys_mq_timedreceive,compat_sys_mq_timedreceive_wrapper)
SYSCALL(sys_mq_notify,sys_mq_notify,compat_sys_mq_notify_wrapper) /* 275 */
SYSCALL(sys_mq_getsetattr,sys_mq_getsetattr,compat_sys_mq_getsetattr_wrapper)
SYSCALL(sys_kexec_load,sys_kexec_load,compat_sys_kexec_load_wrapper)
SYSCALL(sys_add_key,sys_add_key,compat_sys_add_key_wrapper)
SYSCALL(sys_request_key,sys_request_key,compat_sys_request_key_wrapper)
SYSCALL(sys_keyctl,sys_keyctl,compat_sys_keyctl_wrapper)		/* 280 */
SYSCALL(sys_waitid,sys_waitid,compat_sys_waitid_wrapper)
SYSCALL(sys_ioprio_set,sys_ioprio_set,sys_ioprio_set_wrapper)
SYSCALL(sys_ioprio_get,sys_ioprio_get,sys_ioprio_get_wrapper)
SYSCALL(sys_inotify_init,sys_inotify_init,sys_inotify_init)
SYSCALL(sys_inotify_add_watch,sys_inotify_add_watch,sys_inotify_add_watch_wrapper)	/* 285 */
SYSCALL(sys_inotify_rm_watch,sys_inotify_rm_watch,sys_inotify_rm_watch_wrapper)
NI_SYSCALL							/* 287 sys_migrate_pages */
SYSCALL(sys_openat,sys_openat,compat_sys_openat_wrapper)
SYSCALL(sys_mkdirat,sys_mkdirat,sys_mkdirat_wrapper)
SYSCALL(sys_mknodat,sys_mknodat,sys_mknodat_wrapper)	/* 290 */
SYSCALL(sys_fchownat,sys_fchownat,sys_fchownat_wrapper)
SYSCALL(sys_futimesat,sys_futimesat,compat_sys_futimesat_wrapper)
SYSCALL(sys_fstatat64,sys_newfstatat,sys32_fstatat64_wrapper)
SYSCALL(sys_unlinkat,sys_unlinkat,sys_unlinkat_wrapper)
SYSCALL(sys_renameat,sys_renameat,sys_renameat_wrapper)	/* 295 */
SYSCALL(sys_linkat,sys_linkat,sys_linkat_wrapper)
SYSCALL(sys_symlinkat,sys_symlinkat,sys_symlinkat_wrapper)
SYSCALL(sys_readlinkat,sys_readlinkat,sys_readlinkat_wrapper)
SYSCALL(sys_fchmodat,sys_fchmodat,sys_fchmodat_wrapper)
SYSCALL(sys_faccessat,sys_faccessat,sys_faccessat_wrapper)	/* 300 */
SYSCALL(sys_pselect6,sys_pselect6,compat_sys_pselect6_wrapper)
SYSCALL(sys_ppoll,sys_ppoll,compat_sys_ppoll_wrapper)
SYSCALL(sys_unshare,sys_unshare,sys_unshare_wrapper)
SYSCALL(sys_set_robust_list,sys_set_robust_list,compat_sys_set_robust_list_wrapper)
SYSCALL(sys_get_robust_list,sys_get_robust_list,compat_sys_get_robust_list_wrapper)
SYSCALL(sys_splice,sys_splice,sys_splice_wrapper)
SYSCALL(sys_sync_file_range,sys_sync_file_range,sys_sync_file_range_wrapper)
SYSCALL(sys_tee,sys_tee,sys_tee_wrapper)
SYSCALL(sys_vmsplice,sys_vmsplice,compat_sys_vmsplice_wrapper)
NI_SYSCALL							/* 310 sys_move_pages */
SYSCALL(sys_getcpu,sys_getcpu,sys_getcpu_wrapper)
SYSCALL(sys_epoll_pwait,sys_epoll_pwait,compat_sys_epoll_pwait_wrapper)
SYSCALL(sys_utimes,sys_utimes,compat_sys_utimes_wrapper)
SYSCALL(sys_s390_fallocate,sys_fallocate,sys_fallocate_wrapper)
SYSCALL(sys_utimensat,sys_utimensat,compat_sys_utimensat_wrapper)	/* 315 */
SYSCALL(sys_signalfd,sys_signalfd,compat_sys_signalfd_wrapper)
NI_SYSCALL						/* 317 old sys_timer_fd */
SYSCALL(sys_eventfd,sys_eventfd,sys_eventfd_wrapper)
SYSCALL(sys_timerfd_create,sys_timerfd_create,sys_timerfd_create_wrapper)
SYSCALL(sys_timerfd_settime,sys_timerfd_settime,compat_sys_timerfd_settime_wrapper) /* 320 */
SYSCALL(sys_timerfd_gettime,sys_timerfd_gettime,compat_sys_timerfd_gettime_wrapper)
SYSCALL(sys_signalfd4,sys_signalfd4,compat_sys_signalfd4_wrapper)
SYSCALL(sys_eventfd2,sys_eventfd2,sys_eventfd2_wrapper)
SYSCALL(sys_inotify_init1,sys_inotify_init1,sys_inotify_init1_wrapper)
SYSCALL(sys_pipe2,sys_pipe2,sys_pipe2_wrapper) /* 325 */
SYSCALL(sys_dup3,sys_dup3,sys_dup3_wrapper)
SYSCALL(sys_epoll_create1,sys_epoll_create1,sys_epoll_create1_wrapper)
SYSCALL(sys_preadv,sys_preadv,compat_sys_preadv_wrapper)
SYSCALL(sys_pwritev,sys_pwritev,compat_sys_pwritev_wrapper)
SYSCALL(sys_rt_tgsigqueueinfo,sys_rt_tgsigqueueinfo,compat_sys_rt_tgsigqueueinfo_wrapper) /* 330 */
SYSCALL(sys_perf_event_open,sys_perf_event_open,sys_perf_event_open_wrapper)
SYSCALL(sys_fanotify_init,sys_fanotify_init,sys_fanotify_init_wrapper)
SYSCALL(sys_fanotify_mark,sys_fanotify_mark,sys_fanotify_mark_wrapper)
SYSCALL(sys_prlimit64,sys_prlimit64,sys_prlimit64_wrapper)
SYSCALL(sys_name_to_handle_at,sys_name_to_handle_at,sys_name_to_handle_at_wrapper) /* 335 */
SYSCALL(sys_open_by_handle_at,sys_open_by_handle_at,compat_sys_open_by_handle_at_wrapper)
SYSCALL(sys_clock_adjtime,sys_clock_adjtime,compat_sys_clock_adjtime_wrapper)
SYSCALL(sys_syncfs,sys_syncfs,sys_syncfs_wrapper)
SYSCALL(sys_setns,sys_setns,sys_setns_wrapper)
SYSCALL(sys_process_vm_readv,sys_process_vm_readv,compat_sys_process_vm_readv_wrapper) /* 340 */
SYSCALL(sys_process_vm_writev,sys_process_vm_writev,compat_sys_process_vm_writev_wrapper)
SYSCALL(sys_ni_syscall,sys_s390_runtime_instr,sys_s390_runtime_instr_wrapper)
SYSCALL(sys_kcmp,sys_kcmp,sys_kcmp_wrapper)
