/* $Id: systbls.S,v 1.81 2002/02/08 03:57:14 davem Exp $
 * systbls.S: System call entry point tables for OS compatibility.
 *            The native Linux system call table lives here also.
 *
 * Copyright (C) 1995, 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#include <linux/config.h>

	.text
	.align	4

#ifdef CONFIG_COMPAT
	/* First, the 32-bit Linux native syscall table. */

	.globl sys_call_table32
sys_call_table32:
/*0*/	.word sys_restart_syscall, sys32_exit, sys_fork, sys_read, sys_write
/*5*/	.word sys32_open, sys_close, sys32_wait4, sys32_creat, sys_link
/*10*/  .word sys_unlink, sunos_execv, sys_chdir, sys32_chown16, sys32_mknod
/*15*/	.word sys_chmod, sys32_lchown16, sparc_brk, sys32_perfctr, sys32_lseek
/*20*/	.word sys_getpid, sys_capget, sys_capset, sys32_setuid16, sys32_getuid16
/*25*/	.word compat_sys_time, sys_ptrace, sys_alarm, sys32_sigaltstack, sys32_pause
/*30*/	.word compat_sys_utime, sys_lchown, sys_fchown, sys32_access, sys32_nice
	.word sys_chown, sys_sync, sys32_kill, compat_sys_newstat, sys32_sendfile
/*40*/	.word compat_sys_newlstat, sys_dup, sys_pipe, compat_sys_times, sys_getuid
	.word sys32_umount, sys32_setgid16, sys32_getgid16, sys32_signal, sys32_geteuid16
/*50*/	.word sys32_getegid16, sys_acct, sys_nis_syscall, sys_getgid, compat_sys_ioctl
	.word sys32_reboot, sys32_mmap2, sys_symlink, sys32_readlink, sys32_execve
/*60*/	.word sys32_umask, sys_chroot, compat_sys_newfstat, compat_sys_fstat64, sys_getpagesize
	.word sys32_msync, sys_vfork, sys32_pread64, sys32_pwrite64, sys_geteuid
/*70*/	.word sys_getegid, sys_mmap, sys_setreuid, sys_munmap, sys_mprotect
	.word sys_madvise, sys_vhangup, sys32_truncate64, sys_mincore, sys32_getgroups16
/*80*/	.word sys32_setgroups16, sys_getpgrp, sys32_setgroups, sys32_setitimer, sys32_ftruncate64
	.word sys32_swapon, sys32_getitimer, sys_setuid, sys32_sethostname, sys_setgid
/*90*/	.word sys_dup2, sys_setfsuid, compat_sys_fcntl, sys32_select, sys_setfsgid
	.word sys_fsync, sys32_setpriority, sys_nis_syscall, sys_nis_syscall, sys_nis_syscall
/*100*/ .word sys32_getpriority, sys32_rt_sigreturn, sys32_rt_sigaction, sys32_rt_sigprocmask, sys32_rt_sigpending
	.word compat_sys_rt_sigtimedwait, sys32_rt_sigqueueinfo, compat_sys_rt_sigsuspend, sys_setresuid, sys_getresuid
/*110*/	.word sys_setresgid, sys_getresgid, sys_setregid, sys_nis_syscall, sys_nis_syscall
	.word sys32_getgroups, sys32_gettimeofday, sys32_getrusage, sys_nis_syscall, sys_getcwd
/*120*/	.word compat_sys_readv, compat_sys_writev, sys32_settimeofday, sys32_fchown16, sys_fchmod
	.word sys_nis_syscall, sys32_setreuid16, sys32_setregid16, sys_rename, sys_truncate
/*130*/	.word sys_ftruncate, sys_flock, compat_sys_lstat64, sys_nis_syscall, sys_nis_syscall
	.word sys_nis_syscall, sys32_mkdir, sys_rmdir, sys32_utimes, compat_sys_stat64
/*140*/	.word sys32_sendfile64, sys_nis_syscall, sys32_futex, sys_gettid, compat_sys_getrlimit
	.word compat_sys_setrlimit, sys_pivot_root, sys32_prctl, sys_pciconfig_read, sys_pciconfig_write
/*150*/	.word sys_nis_syscall, sys_inotify_init, sys_inotify_add_watch, sys_poll, sys_getdents64
	.word compat_sys_fcntl64, sys_inotify_rm_watch, compat_sys_statfs, compat_sys_fstatfs, sys_oldumount
/*160*/	.word compat_sys_sched_setaffinity, compat_sys_sched_getaffinity, sys32_getdomainname, sys32_setdomainname, sys_nis_syscall
	.word sys_quotactl, sys_set_tid_address, compat_sys_mount, sys_ustat, sys32_setxattr
/*170*/	.word sys32_lsetxattr, sys32_fsetxattr, sys_getxattr, sys_lgetxattr, compat_sys_getdents
	.word sys_setsid, sys_fchdir, sys32_fgetxattr, sys_listxattr, sys_llistxattr
/*180*/	.word sys32_flistxattr, sys_removexattr, sys_lremovexattr, compat_sys_sigpending, sys_ni_syscall
	.word sys32_setpgid, sys32_fremovexattr, sys32_tkill, sys32_exit_group, sparc64_newuname
/*190*/	.word sys32_init_module, sparc64_personality, sys_remap_file_pages, sys32_epoll_create, sys32_epoll_ctl
	.word sys32_epoll_wait, sys32_ioprio_set, sys_getppid, sys32_sigaction, sys_sgetmask
/*200*/	.word sys32_ssetmask, sys_sigsuspend, compat_sys_newlstat, sys_uselib, compat_sys_old_readdir
	.word sys32_readahead, sys32_socketcall, sys32_syslog, sys32_lookup_dcookie, sys32_fadvise64
/*210*/	.word sys32_fadvise64_64, sys32_tgkill, sys32_waitpid, sys_swapoff, sys32_sysinfo
	.word sys32_ipc, sys32_sigreturn, sys_clone, sys32_ioprio_get, compat_sys_adjtimex
/*220*/	.word sys32_sigprocmask, sys_ni_syscall, sys32_delete_module, sys_ni_syscall, sys32_getpgid
	.word sys32_bdflush, sys32_sysfs, sys_nis_syscall, sys32_setfsuid16, sys32_setfsgid16
/*230*/	.word sys32_select, compat_sys_time, sys_nis_syscall, compat_sys_stime, compat_sys_statfs64
	.word compat_sys_fstatfs64, sys_llseek, sys_mlock, sys_munlock, sys32_mlockall
/*240*/	.word sys_munlockall, sys32_sched_setparam, sys32_sched_getparam, sys32_sched_setscheduler, sys32_sched_getscheduler
	.word sys_sched_yield, sys32_sched_get_priority_max, sys32_sched_get_priority_min, sys32_sched_rr_get_interval, compat_sys_nanosleep
/*250*/	.word sys32_mremap, sys32_sysctl, sys32_getsid, sys_fdatasync, sys32_nfsservctl
	.word sys_ni_syscall, compat_sys_clock_settime, compat_sys_clock_gettime, compat_sys_clock_getres, sys32_clock_nanosleep
/*260*/	.word compat_sys_sched_getaffinity, compat_sys_sched_setaffinity, sys32_timer_settime, compat_sys_timer_gettime, sys_timer_getoverrun
	.word sys_timer_delete, compat_sys_timer_create, sys_ni_syscall, compat_sys_io_setup, sys_io_destroy
/*270*/	.word sys32_io_submit, sys_io_cancel, compat_sys_io_getevents, sys32_mq_open, sys_mq_unlink
	.word compat_sys_mq_timedsend, compat_sys_mq_timedreceive, compat_sys_mq_notify, compat_sys_mq_getsetattr, compat_sys_waitid
/*280*/	.word sys_ni_syscall, sys_add_key, sys_request_key, sys_keyctl, compat_sys_openat
	.word sys_mkdirat, sys_mknodat, sys_fchownat, compat_sys_futimesat, compat_sys_fstatat64
/*285*/	.word sys_unlinkat, sys_renameat, sys_linkat, sys_symlinkat, sys_readlinkat
	.word sys_fchmodat, sys_faccessat, compat_sys_pselect6, compat_sys_ppoll, sys_unshare

#endif /* CONFIG_COMPAT */

	/* Now the 64-bit native Linux syscall table. */

	.align	4
	.globl sys_call_table64, sys_call_table
sys_call_table64:
sys_call_table:
/*0*/	.word sys_restart_syscall, sparc_exit, sys_fork, sys_read, sys_write
/*5*/	.word sys_open, sys_close, sys_wait4, sys_creat, sys_link
/*10*/  .word sys_unlink, sys_nis_syscall, sys_chdir, sys_chown, sys_mknod
/*15*/	.word sys_chmod, sys_lchown, sparc_brk, sys_perfctr, sys_lseek
/*20*/	.word sys_getpid, sys_capget, sys_capset, sys_setuid, sys_getuid
/*25*/	.word sys_nis_syscall, sys_ptrace, sys_alarm, sys_sigaltstack, sys_nis_syscall
/*30*/	.word sys_utime, sys_nis_syscall, sys_nis_syscall, sys_access, sys_nice
	.word sys_nis_syscall, sys_sync, sys_kill, sys_newstat, sys_sendfile64
/*40*/	.word sys_newlstat, sys_dup, sys_pipe, sys_times, sys_nis_syscall
	.word sys_umount, sys_setgid, sys_getgid, sys_signal, sys_geteuid
/*50*/	.word sys_getegid, sys_acct, sys_memory_ordering, sys_nis_syscall, sys_ioctl
	.word sys_reboot, sys_nis_syscall, sys_symlink, sys_readlink, sys_execve
/*60*/	.word sys_umask, sys_chroot, sys_newfstat, sys_fstat64, sys_getpagesize
	.word sys_msync, sys_vfork, sys_pread64, sys_pwrite64, sys_nis_syscall
/*70*/	.word sys_nis_syscall, sys_mmap, sys_nis_syscall, sys64_munmap, sys_mprotect
	.word sys_madvise, sys_vhangup, sys_nis_syscall, sys_mincore, sys_getgroups
/*80*/	.word sys_setgroups, sys_getpgrp, sys_nis_syscall, sys_setitimer, sys_nis_syscall
	.word sys_swapon, sys_getitimer, sys_nis_syscall, sys_sethostname, sys_nis_syscall
/*90*/	.word sys_dup2, sys_nis_syscall, sys_fcntl, sys_select, sys_nis_syscall
	.word sys_fsync, sys_setpriority, sys_socket, sys_connect, sys_accept
/*100*/	.word sys_getpriority, sys_rt_sigreturn, sys_rt_sigaction, sys_rt_sigprocmask, sys_rt_sigpending
	.word sys_rt_sigtimedwait, sys_rt_sigqueueinfo, sys_rt_sigsuspend, sys_setresuid, sys_getresuid
/*110*/	.word sys_setresgid, sys_getresgid, sys_nis_syscall, sys_recvmsg, sys_sendmsg
	.word sys_nis_syscall, sys_gettimeofday, sys_getrusage, sys_getsockopt, sys_getcwd
/*120*/	.word sys_readv, sys_writev, sys_settimeofday, sys_fchown, sys_fchmod
	.word sys_recvfrom, sys_setreuid, sys_setregid, sys_rename, sys_truncate
/*130*/	.word sys_ftruncate, sys_flock, sys_lstat64, sys_sendto, sys_shutdown
	.word sys_socketpair, sys_mkdir, sys_rmdir, sys_utimes, sys_stat64
/*140*/	.word sys_sendfile64, sys_getpeername, sys_futex, sys_gettid, sys_getrlimit
	.word sys_setrlimit, sys_pivot_root, sys_prctl, sys_pciconfig_read, sys_pciconfig_write
/*150*/	.word sys_getsockname, sys_inotify_init, sys_inotify_add_watch, sys_poll, sys_getdents64
	.word sys_nis_syscall, sys_inotify_rm_watch, sys_statfs, sys_fstatfs, sys_oldumount
/*160*/	.word sys_sched_setaffinity, sys_sched_getaffinity, sys_getdomainname, sys_setdomainname, sys_utrap_install
	.word sys_quotactl, sys_set_tid_address, sys_mount, sys_ustat, sys_setxattr
/*170*/	.word sys_lsetxattr, sys_fsetxattr, sys_getxattr, sys_lgetxattr, sys_getdents
	.word sys_setsid, sys_fchdir, sys_fgetxattr, sys_listxattr, sys_llistxattr
/*180*/	.word sys_flistxattr, sys_removexattr, sys_lremovexattr, sys_nis_syscall, sys_ni_syscall
	.word sys_setpgid, sys_fremovexattr, sys_tkill, sys_exit_group, sparc64_newuname
/*190*/	.word sys_init_module, sparc64_personality, sys_remap_file_pages, sys_epoll_create, sys_epoll_ctl
	.word sys_epoll_wait, sys_ioprio_set, sys_getppid, sys_nis_syscall, sys_sgetmask
/*200*/	.word sys_ssetmask, sys_nis_syscall, sys_newlstat, sys_uselib, sys_nis_syscall
	.word sys_readahead, sys_socketcall, sys_syslog, sys_lookup_dcookie, sys_fadvise64
/*210*/	.word sys_fadvise64_64, sys_tgkill, sys_waitpid, sys_swapoff, sys_sysinfo
	.word sys_ipc, sys_nis_syscall, sys_clone, sys_ioprio_get, sys_adjtimex
/*220*/	.word sys_nis_syscall, sys_ni_syscall, sys_delete_module, sys_ni_syscall, sys_getpgid
	.word sys_bdflush, sys_sysfs, sys_nis_syscall, sys_setfsuid, sys_setfsgid
/*230*/	.word sys_select, sys_nis_syscall, sys_nis_syscall, sys_stime, sys_statfs64
	.word sys_fstatfs64, sys_llseek, sys_mlock, sys_munlock, sys_mlockall
/*240*/	.word sys_munlockall, sys_sched_setparam, sys_sched_getparam, sys_sched_setscheduler, sys_sched_getscheduler
	.word sys_sched_yield, sys_sched_get_priority_max, sys_sched_get_priority_min, sys_sched_rr_get_interval, sys_nanosleep
/*250*/	.word sys64_mremap, sys_sysctl, sys_getsid, sys_fdatasync, sys_nfsservctl
	.word sys_ni_syscall, sys_clock_settime, sys_clock_gettime, sys_clock_getres, sys_clock_nanosleep
/*260*/	.word sys_sched_getaffinity, sys_sched_setaffinity, sys_timer_settime, sys_timer_gettime, sys_timer_getoverrun
	.word sys_timer_delete, sys_timer_create, sys_ni_syscall, sys_io_setup, sys_io_destroy
/*270*/	.word sys_io_submit, sys_io_cancel, sys_io_getevents, sys_mq_open, sys_mq_unlink
	.word sys_mq_timedsend, sys_mq_timedreceive, sys_mq_notify, sys_mq_getsetattr, sys_waitid
/*280*/	.word sys_nis_syscall, sys_add_key, sys_request_key, sys_keyctl, sys_openat
	.word sys_mkdirat, sys_mknodat, sys_fchownat, sys_futimesat, sys_fstatat64
/*285*/	.word sys_unlinkat, sys_renameat, sys_linkat, sys_symlinkat, sys_readlinkat
	.word sys_fchmodat, sys_faccessat, sys_pselect6, sys_ppoll, sys_unshare

#if defined(CONFIG_SUNOS_EMUL) || defined(CONFIG_SOLARIS_EMUL) || \
    defined(CONFIG_SOLARIS_EMUL_MODULE)
	/* Now the 32-bit SunOS syscall table. */

	.align 4
	.globl sunos_sys_table
sunos_sys_table:
/*0*/	.word sunos_indir, sys32_exit, sys_fork
	.word sunos_read, sunos_write, sunos_open
	.word sys_close, sunos_wait4, sys_creat
	.word sys_link, sys_unlink, sunos_execv
	.word sys_chdir, sunos_nosys, sys32_mknod
	.word sys_chmod, sys32_lchown16, sunos_brk
	.word sunos_nosys, sys32_lseek, sunos_getpid
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_getuid, sunos_nosys, sys_ptrace
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sys_access, sunos_nosys, sunos_nosys
	.word sys_sync, sys_kill, compat_sys_newstat
	.word sunos_nosys, compat_sys_newlstat, sys_dup
	.word sys_pipe, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_getgid
	.word sunos_nosys, sunos_nosys
/*50*/	.word sunos_nosys, sys_acct, sunos_nosys
	.word sunos_mctl, sunos_ioctl, sys_reboot
	.word sunos_nosys, sys_symlink, sys_readlink
	.word sys32_execve, sys_umask, sys_chroot
	.word compat_sys_newfstat, sunos_nosys, sys_getpagesize
	.word sys_msync, sys_vfork, sunos_nosys
	.word sunos_nosys, sunos_sbrk, sunos_sstk
	.word sunos_mmap, sunos_vadvise, sys_munmap
	.word sys_mprotect, sys_madvise, sys_vhangup
	.word sunos_nosys, sys_mincore, sys32_getgroups16
	.word sys32_setgroups16, sys_getpgrp, sunos_setpgrp
	.word compat_sys_setitimer, sunos_nosys, sys_swapon
	.word compat_sys_getitimer, sys_gethostname, sys_sethostname
	.word sunos_getdtablesize, sys_dup2, sunos_nop
	.word compat_sys_fcntl, sunos_select, sunos_nop
	.word sys_fsync, sys32_setpriority, sys32_socket
	.word sys32_connect, sunos_accept
/*100*/	.word sys_getpriority, sunos_send, sunos_recv
	.word sunos_nosys, sys32_bind, sunos_setsockopt
	.word sys32_listen, sunos_nosys, sunos_sigaction
	.word sunos_sigblock, sunos_sigsetmask, sys_sigpause
	.word sys32_sigstack, sys32_recvmsg, sys32_sendmsg
	.word sunos_nosys, sys32_gettimeofday, compat_sys_getrusage
	.word sunos_getsockopt, sunos_nosys, sunos_readv
	.word sunos_writev, sys32_settimeofday, sys32_fchown16
	.word sys_fchmod, sys32_recvfrom, sys32_setreuid16
	.word sys32_setregid16, sys_rename, sys_truncate
	.word sys_ftruncate, sys_flock, sunos_nosys
	.word sys32_sendto, sys32_shutdown, sys32_socketpair
	.word sys_mkdir, sys_rmdir, sys32_utimes
	.word sys32_sigreturn, sunos_nosys, sys32_getpeername
	.word sunos_gethostid, sunos_nosys, compat_sys_getrlimit
	.word compat_sys_setrlimit, sunos_killpg, sunos_nosys
	.word sunos_nosys, sunos_nosys
/*150*/	.word sys32_getsockname, sunos_nosys, sunos_nosys
	.word sys_poll, sunos_nosys, sunos_nosys
	.word sunos_getdirentries, compat_sys_statfs, compat_sys_fstatfs
	.word sys_oldumount, sunos_nosys, sunos_nosys
	.word sys_getdomainname, sys_setdomainname
	.word sunos_nosys, sys_quotactl, sunos_nosys
	.word sunos_nosys, sys_ustat, sunos_semsys
	.word sunos_nosys, sunos_shmsys, sunos_audit
	.word sunos_nosys, sunos_getdents, sys_setsid
	.word sys_fchdir, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, compat_sys_sigpending, sunos_nosys
	.word sys_setpgid, sunos_pathconf, sunos_fpathconf
	.word sunos_sysconf, sunos_uname, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
/*200*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys
/*250*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys
/*260*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys
/*270*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys
/*280*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys
/*290*/	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys, sunos_nosys, sunos_nosys
	.word sunos_nosys
#endif
