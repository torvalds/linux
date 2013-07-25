/* Systemcall Entry Table */
#include <linux/sys.h>
#include <asm/linkage.h>
#include <asm/unistd.h>

#define CALL(x)	.long _ ## x

.globl _sys_call_table

#if defined(CONFIG_CPU_H8300H)
	.h8300h
#endif
#if defined(CONFIG_CPU_H8S)
	.h8300s
#endif
	.section .text
	.align	2
_sys_call_table:
	CALL(sys_ni_syscall)		/* 0  -  old "setup()" system call*/
	CALL(sys_exit)
	CALL(sys_fork)
	CALL(sys_read)
	CALL(sys_write)
	CALL(sys_open)			/* 5 */
	CALL(sys_close)
	CALL(sys_waitpid)
	CALL(sys_creat)
	CALL(sys_link)
	CALL(sys_unlink)		/* 10 */
	CALL(sys_execve)
	CALL(sys_chdir)
	CALL(sys_time)
	CALL(sys_mknod)
	CALL(sys_chmod)			/* 15 */
	CALL(sys_chown16)
	CALL(sys_ni_syscall)		/* old break syscall holder */
	CALL(sys_stat)
	CALL(sys_lseek)
	CALL(sys_getpid)		/* 20 */
	CALL(sys_mount)
	CALL(sys_oldumount)
	CALL(sys_setuid16)
	CALL(sys_getuid16)
	CALL(sys_stime)			/* 25 */
	CALL(sys_ptrace)
	CALL(sys_alarm)
	CALL(sys_fstat)
	CALL(sys_pause)
	CALL(sys_utime)			/* 30 */
	CALL(sys_ni_syscall)		/* old stty syscall holder */
	CALL(sys_ni_syscall)		/* old gtty syscall holder */
	CALL(sys_access)
	CALL(sys_nice)
	CALL(sys_ni_syscall)		/* 35 old ftime syscall holder */
	CALL(sys_sync)
	CALL(sys_kill)
	CALL(sys_rename)
	CALL(sys_mkdir)
	CALL(sys_rmdir)			/* 40 */
	CALL(sys_dup)
	CALL(sys_pipe)
	CALL(sys_times)
	CALL(sys_ni_syscall)		/* old prof syscall holder */
	CALL(sys_brk)			/* 45 */
	CALL(sys_setgid16)
	CALL(sys_getgid16)
	CALL(sys_signal)
	CALL(sys_geteuid16)
	CALL(sys_getegid16)		/* 50 */
	CALL(sys_acct)
	CALL(sys_umount)		/* recycled never used phys() */
	CALL(sys_ni_syscall)		/* old lock syscall holder */
	CALL(sys_ioctl)
	CALL(sys_fcntl)			/* 55 */
	CALL(sys_ni_syscall)		/* old mpx syscall holder */
	CALL(sys_setpgid)
	CALL(sys_ni_syscall)		/* old ulimit syscall holder */
	CALL(sys_ni_syscall)
	CALL(sys_umask)			/* 60 */
	CALL(sys_chroot)
	CALL(sys_ustat)
	CALL(sys_dup2)
	CALL(sys_getppid)
	CALL(sys_getpgrp)		/* 65 */
	CALL(sys_setsid)
	CALL(sys_sigaction)
	CALL(sys_sgetmask)
	CALL(sys_ssetmask)
	CALL(sys_setreuid16)		/* 70 */
	CALL(sys_setregid16)
	CALL(sys_sigsuspend)
	CALL(sys_sigpending)
	CALL(sys_sethostname)
	CALL(sys_setrlimit)		/* 75 */
	CALL(sys_old_getrlimit)
	CALL(sys_getrusage)
	CALL(sys_gettimeofday)
	CALL(sys_settimeofday)
	CALL(sys_getgroups16)		/* 80 */
	CALL(sys_setgroups16)
	CALL(sys_old_select)
	CALL(sys_symlink)
	CALL(sys_lstat)
	CALL(sys_readlink)		/* 85 */
	CALL(sys_uselib)
	CALL(sys_swapon)
	CALL(sys_reboot)
	CALL(sys_old_readdir)
	CALL(sys_old_mmap)		/* 90 */
	CALL(sys_munmap)
	CALL(sys_truncate)
	CALL(sys_ftruncate)
	CALL(sys_fchmod)
	CALL(sys_fchown16)		/* 95 */
	CALL(sys_getpriority)
	CALL(sys_setpriority)
	CALL(sys_ni_syscall)		/* old profil syscall holder */
	CALL(sys_statfs)
	CALL(sys_fstatfs)		/* 100 */
	CALL(sys_ni_syscall)		/* ioperm for i386 */
	CALL(sys_socketcall)
	CALL(sys_syslog)
	CALL(sys_setitimer)
	CALL(sys_getitimer)		/* 105 */
	CALL(sys_newstat)
	CALL(sys_newlstat)
	CALL(sys_newfstat)
	CALL(sys_ni_syscall)
	CALL(sys_ni_syscall)		/* iopl for i386 */ /* 110 */
	CALL(sys_vhangup)
	CALL(sys_ni_syscall)		/* obsolete idle() syscall */
	CALL(sys_ni_syscall)		/* vm86old for i386 */
	CALL(sys_wait4)
	CALL(sys_swapoff)		/* 115 */
	CALL(sys_sysinfo)
	CALL(sys_ipc)
	CALL(sys_fsync)
	CALL(sys_sigreturn)
	CALL(sys_clone)			/* 120 */
	CALL(sys_setdomainname)
	CALL(sys_newuname)
	CALL(sys_cacheflush)		/* modify_ldt for i386 */
	CALL(sys_adjtimex)
	CALL(sys_ni_syscall)		/* 125 sys_mprotect */
	CALL(sys_sigprocmask)
	CALL(sys_ni_syscall)		/* sys_create_module */
	CALL(sys_init_module)
	CALL(sys_delete_module)
	CALL(sys_ni_syscall)		/* 130 sys_get_kernel_syms */
	CALL(sys_quotactl)
	CALL(sys_getpgid)
	CALL(sys_fchdir)
	CALL(sys_bdflush)
	CALL(sys_sysfs)			/* 135 */
	CALL(sys_personality)
	CALL(sys_ni_syscall)		/* for afs_syscall */
	CALL(sys_setfsuid16)
	CALL(sys_setfsgid16)
	CALL(sys_llseek)		/* 140 */
	CALL(sys_getdents)
	CALL(sys_select)
	CALL(sys_flock)
	CALL(sys_ni_syscall)		/* sys_msync */
	CALL(sys_readv)			/* 145 */
	CALL(sys_writev)
	CALL(sys_getsid)
	CALL(sys_fdatasync)
	CALL(sys_sysctl)
	CALL(sys_ni_syscall)		/* 150 sys_mlock */
	CALL(sys_ni_syscall)		/* sys_munlock */
	CALL(sys_ni_syscall)		/* sys_mlockall */
	CALL(sys_ni_syscall)		/* sys_munlockall */
	CALL(sys_sched_setparam)
	CALL(sys_sched_getparam)	/* 155 */
	CALL(sys_sched_setscheduler)
	CALL(sys_sched_getscheduler)
	CALL(sys_sched_yield)
	CALL(sys_sched_get_priority_max)
	CALL(sys_sched_get_priority_min)  /* 160 */
	CALL(sys_sched_rr_get_interval)
	CALL(sys_nanosleep)
	CALL(sys_ni_syscall)		/* sys_mremap */
	CALL(sys_setresuid16)
	CALL(sys_getresuid16)		/* 165 */
	CALL(sys_ni_syscall)		/* for vm86 */
	CALL(sys_ni_syscall)		/* sys_query_module */
	CALL(sys_poll)
	CALL(sys_ni_syscall)		/* old nfsservctl */
	CALL(sys_setresgid16)		/* 170 */
	CALL(sys_getresgid16)
	CALL(sys_prctl)
	CALL(sys_rt_sigreturn)
	CALL(sys_rt_sigaction)
	CALL(sys_rt_sigprocmask)	/* 175 */
	CALL(sys_rt_sigpending)
	CALL(sys_rt_sigtimedwait)
	CALL(sys_rt_sigqueueinfo)
	CALL(sys_rt_sigsuspend)
	CALL(sys_pread64)		/* 180 */
	CALL(sys_pwrite64)
	CALL(sys_lchown16);
	CALL(sys_getcwd)
	CALL(sys_capget)
	CALL(sys_capset)		/* 185 */
	CALL(sys_sigaltstack)
	CALL(sys_sendfile)
	CALL(sys_ni_syscall)		/* streams1 */
	CALL(sys_ni_syscall)		/* streams2 */
	CALL(sys_vfork)			/* 190 */
	CALL(sys_getrlimit)
	CALL(sys_mmap_pgoff)
	CALL(sys_truncate64)
	CALL(sys_ftruncate64)
	CALL(sys_stat64)		/* 195 */
	CALL(sys_lstat64)
	CALL(sys_fstat64)
	CALL(sys_chown)
	CALL(sys_getuid)
	CALL(sys_getgid)		/* 200 */
	CALL(sys_geteuid)
	CALL(sys_getegid)
	CALL(sys_setreuid)
	CALL(sys_setregid)
	CALL(sys_getgroups)		/* 205 */
	CALL(sys_setgroups)
	CALL(sys_fchown)
	CALL(sys_setresuid)
	CALL(sys_getresuid)
	CALL(sys_setresgid)		/* 210 */
	CALL(sys_getresgid)
	CALL(sys_lchown)
	CALL(sys_setuid)
	CALL(sys_setgid)
	CALL(sys_setfsuid)		/* 215 */
	CALL(sys_setfsgid)
	CALL(sys_pivot_root)
	CALL(sys_ni_syscall)
	CALL(sys_ni_syscall)
	CALL(sys_getdents64)		/* 220 */
	CALL(sys_fcntl64)
	CALL(sys_ni_syscall)		/* reserved TUX */
	CALL(sys_ni_syscall)		/* reserved Security */
	CALL(sys_gettid)
	CALL(sys_readahead)		/* 225 */
	CALL(sys_setxattr)
	CALL(sys_lsetxattr)
	CALL(sys_fsetxattr)
	CALL(sys_getxattr)
	CALL(sys_lgetxattr)		/* 230 */
	CALL(sys_fgetxattr)
	CALL(sys_listxattr)
	CALL(sys_llistxattr)
	CALL(sys_flistxattr)
	CALL(sys_removexattr)		/* 235 */
	CALL(sys_lremovexattr)
	CALL(sys_fremovexattr)
	CALL(sys_tkill)
	CALL(sys_sendfile64)
	CALL(sys_futex)			/* 240 */
	CALL(sys_sched_setaffinity)
	CALL(sys_sched_getaffinity)
	CALL(sys_ni_syscall)
	CALL(sys_ni_syscall)
	CALL(sys_io_setup)		/* 245 */
	CALL(sys_io_destroy)
	CALL(sys_io_getevents)
	CALL(sys_io_submit)
	CALL(sys_io_cancel)
	CALL(sys_fadvise64)		/* 250 */
	CALL(sys_ni_syscall)
	CALL(sys_exit_group)
	CALL(sys_lookup_dcookie)
	CALL(sys_epoll_create)
	CALL(sys_epoll_ctl)		/* 255 */
	CALL(sys_epoll_wait)
	CALL(sys_ni_syscall)		/* sys_remap_file_pages */
	CALL(sys_set_tid_address)
	CALL(sys_timer_create)
	CALL(sys_timer_settime)		/* 260 */
	CALL(sys_timer_gettime)
	CALL(sys_timer_getoverrun)
	CALL(sys_timer_delete)
	CALL(sys_clock_settime)
	CALL(sys_clock_gettime)		/* 265 */
	CALL(sys_clock_getres)
	CALL(sys_clock_nanosleep)
	CALL(sys_statfs64)
	CALL(sys_fstatfs64)
	CALL(sys_tgkill)		/* 270 */
	CALL(sys_utimes)
	CALL(sys_fadvise64_64)
	CALL(sys_ni_syscall)		/* sys_vserver */
	CALL(sys_ni_syscall)
	CALL(sys_get_mempolicy)		/* 275 */
	CALL(sys_set_mempolicy)
	CALL(sys_mq_open)
	CALL(sys_mq_unlink)
	CALL(sys_mq_timedsend)
	CALL(sys_mq_timedreceive)	/* 280 */
	CALL(sys_mq_notify)
	CALL(sys_mq_getsetattr)
	CALL(sys_waitid)
	CALL(sys_ni_syscall)		/* sys_kexec_load */
	CALL(sys_add_key) 		/* 285 */
	CALL(sys_request_key)
	CALL(sys_keyctl)
	CALL(sys_ioprio_set)
	CALL(sys_ioprio_get)		/* 290 */
	CALL(sys_inotify_init)
	CALL(sys_inotify_add_watch)
	CALL(sys_inotify_rm_watch)
	CALL(sys_migrate_pages)
	CALL(sys_openat)		/* 295 */
	CALL(sys_mkdirat)
	CALL(sys_mknodat)
	CALL(sys_fchownat)
	CALL(sys_futimesat)
	CALL(sys_fstatat64)		/* 300 */
	CALL(sys_unlinkat)
	CALL(sys_renameat)
	CALL(sys_linkat)
	CALL(sys_symlinkat)
	CALL(sys_readlinkat)		/* 305 */
	CALL(sys_fchmodat)
	CALL(sys_faccessat)
	CALL(sys_ni_syscall)		/* sys_pselect6 */
	CALL(sys_ni_syscall)		/* sys_ppoll */
	CALL(sys_unshare)		/* 310 */
	CALL(sys_set_robust_list)
	CALL(sys_get_robust_list)
	CALL(sys_splice)
	CALL(sys_sync_file_range)
	CALL(sys_tee)			/* 315 */
	CALL(sys_vmsplice)
	CALL(sys_ni_syscall)		/* sys_move_pages */
	CALL(sys_getcpu)
	CALL(sys_ni_syscall)		/* sys_epoll_pwait */
	CALL(sys_setns)			/* 320 */
