/* Systemcall Entry Table */
#include <linux/sys.h>
#include <asm/linkage.h>
#include <asm/unistd.h>
	
.globl SYMBOL_NAME(sys_call_table)

#if defined(CONFIG_CPU_H8300H)
	.h8300h
#endif
#if defined(CONFIG_CPU_H8S)
	.h8300s
#endif
	.section .text
	.align	2
SYMBOL_NAME_LABEL(sys_call_table)	
	.long SYMBOL_NAME(sys_ni_syscall)	/* 0  -  old "setup()" system call*/
	.long SYMBOL_NAME(sys_exit)
	.long SYMBOL_NAME(sys_fork)
	.long SYMBOL_NAME(sys_read)
	.long SYMBOL_NAME(sys_write)
	.long SYMBOL_NAME(sys_open)		/* 5 */
	.long SYMBOL_NAME(sys_close)
	.long SYMBOL_NAME(sys_waitpid)
	.long SYMBOL_NAME(sys_creat)
	.long SYMBOL_NAME(sys_link)
	.long SYMBOL_NAME(sys_unlink)		/* 10 */
	.long SYMBOL_NAME(sys_execve)
	.long SYMBOL_NAME(sys_chdir)
	.long SYMBOL_NAME(sys_time)
	.long SYMBOL_NAME(sys_mknod)
	.long SYMBOL_NAME(sys_chmod)		/* 15 */
	.long SYMBOL_NAME(sys_chown16)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old break syscall holder */
	.long SYMBOL_NAME(sys_stat)
	.long SYMBOL_NAME(sys_lseek)
	.long SYMBOL_NAME(sys_getpid)		/* 20 */
	.long SYMBOL_NAME(sys_mount)
	.long SYMBOL_NAME(sys_oldumount)
	.long SYMBOL_NAME(sys_setuid16)
	.long SYMBOL_NAME(sys_getuid16)
	.long SYMBOL_NAME(sys_stime)		/* 25 */
	.long SYMBOL_NAME(sys_ptrace)
	.long SYMBOL_NAME(sys_alarm)
	.long SYMBOL_NAME(sys_fstat)
	.long SYMBOL_NAME(sys_pause)
	.long SYMBOL_NAME(sys_utime)		/* 30 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old stty syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old gtty syscall holder */
	.long SYMBOL_NAME(sys_access)
	.long SYMBOL_NAME(sys_nice)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 35 old ftime syscall holder */
	.long SYMBOL_NAME(sys_sync)
	.long SYMBOL_NAME(sys_kill)
	.long SYMBOL_NAME(sys_rename)
	.long SYMBOL_NAME(sys_mkdir)
	.long SYMBOL_NAME(sys_rmdir)		/* 40 */
	.long SYMBOL_NAME(sys_dup)
	.long SYMBOL_NAME(sys_pipe)
	.long SYMBOL_NAME(sys_times)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old prof syscall holder */
	.long SYMBOL_NAME(sys_brk)		/* 45 */
	.long SYMBOL_NAME(sys_setgid16)
	.long SYMBOL_NAME(sys_getgid16)
	.long SYMBOL_NAME(sys_signal)
	.long SYMBOL_NAME(sys_geteuid16)
	.long SYMBOL_NAME(sys_getegid16)	/* 50 */
	.long SYMBOL_NAME(sys_acct)
	.long SYMBOL_NAME(sys_umount)		/* recycled never used phys() */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old lock syscall holder */
	.long SYMBOL_NAME(sys_ioctl)
	.long SYMBOL_NAME(sys_fcntl)		/* 55 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old mpx syscall holder */
	.long SYMBOL_NAME(sys_setpgid)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old ulimit syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_umask)		/* 60 */
	.long SYMBOL_NAME(sys_chroot)
	.long SYMBOL_NAME(sys_ustat)
	.long SYMBOL_NAME(sys_dup2)
	.long SYMBOL_NAME(sys_getppid)
	.long SYMBOL_NAME(sys_getpgrp)		/* 65 */
	.long SYMBOL_NAME(sys_setsid)
	.long SYMBOL_NAME(sys_sigaction)
	.long SYMBOL_NAME(sys_sgetmask)
	.long SYMBOL_NAME(sys_ssetmask)
	.long SYMBOL_NAME(sys_setreuid16)	/* 70 */
	.long SYMBOL_NAME(sys_setregid16)
	.long SYMBOL_NAME(sys_sigsuspend)
	.long SYMBOL_NAME(sys_sigpending)
	.long SYMBOL_NAME(sys_sethostname)
	.long SYMBOL_NAME(sys_setrlimit)	/* 75 */
	.long SYMBOL_NAME(sys_old_getrlimit)
	.long SYMBOL_NAME(sys_getrusage)
	.long SYMBOL_NAME(sys_gettimeofday)
	.long SYMBOL_NAME(sys_settimeofday)
	.long SYMBOL_NAME(sys_getgroups16)	/* 80 */
	.long SYMBOL_NAME(sys_setgroups16)
	.long SYMBOL_NAME(old_select)
	.long SYMBOL_NAME(sys_symlink)
	.long SYMBOL_NAME(sys_lstat)
	.long SYMBOL_NAME(sys_readlink)		/* 85 */
	.long SYMBOL_NAME(sys_uselib)
	.long SYMBOL_NAME(sys_swapon)
	.long SYMBOL_NAME(sys_reboot)
	.long SYMBOL_NAME(sys_old_readdir)
	.long SYMBOL_NAME(old_mmap)		/* 90 */
	.long SYMBOL_NAME(sys_munmap)
	.long SYMBOL_NAME(sys_truncate)
	.long SYMBOL_NAME(sys_ftruncate)
	.long SYMBOL_NAME(sys_fchmod)
	.long SYMBOL_NAME(sys_fchown16)		/* 95 */
	.long SYMBOL_NAME(sys_getpriority)
	.long SYMBOL_NAME(sys_setpriority)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old profil syscall holder */
	.long SYMBOL_NAME(sys_statfs)
	.long SYMBOL_NAME(sys_fstatfs)		/* 100 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* ioperm for i386 */
	.long SYMBOL_NAME(sys_socketcall)
	.long SYMBOL_NAME(sys_syslog)
	.long SYMBOL_NAME(sys_setitimer)
	.long SYMBOL_NAME(sys_getitimer)	/* 105 */
	.long SYMBOL_NAME(sys_newstat)
	.long SYMBOL_NAME(sys_newlstat)
	.long SYMBOL_NAME(sys_newfstat)
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_ni_syscall)	/* iopl for i386 */ /* 110 */
	.long SYMBOL_NAME(sys_vhangup)
	.long SYMBOL_NAME(sys_ni_syscall)	/* obsolete idle() syscall */
	.long SYMBOL_NAME(sys_ni_syscall)	/* vm86old for i386 */
	.long SYMBOL_NAME(sys_wait4)
	.long SYMBOL_NAME(sys_swapoff)		/* 115 */
	.long SYMBOL_NAME(sys_sysinfo)
	.long SYMBOL_NAME(sys_ipc)
	.long SYMBOL_NAME(sys_fsync)
	.long SYMBOL_NAME(sys_sigreturn)
	.long SYMBOL_NAME(sys_clone)		/* 120 */
	.long SYMBOL_NAME(sys_setdomainname)
	.long SYMBOL_NAME(sys_newuname)
	.long SYMBOL_NAME(sys_cacheflush)	/* modify_ldt for i386 */
	.long SYMBOL_NAME(sys_adjtimex)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 125 sys_mprotect */
	.long SYMBOL_NAME(sys_sigprocmask)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_create_module */
	.long SYMBOL_NAME(sys_init_module)
	.long SYMBOL_NAME(sys_delete_module)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 130 sys_get_kernel_syms */
	.long SYMBOL_NAME(sys_quotactl)
	.long SYMBOL_NAME(sys_getpgid)
	.long SYMBOL_NAME(sys_fchdir)
	.long SYMBOL_NAME(sys_bdflush)
	.long SYMBOL_NAME(sys_sysfs)		/* 135 */
	.long SYMBOL_NAME(sys_personality)
	.long SYMBOL_NAME(sys_ni_syscall)	/* for afs_syscall */
	.long SYMBOL_NAME(sys_setfsuid16)
	.long SYMBOL_NAME(sys_setfsgid16)
	.long SYMBOL_NAME(sys_llseek)		/* 140 */
	.long SYMBOL_NAME(sys_getdents)
	.long SYMBOL_NAME(sys_select)
	.long SYMBOL_NAME(sys_flock)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_msync */
	.long SYMBOL_NAME(sys_readv)		/* 145 */
	.long SYMBOL_NAME(sys_writev)
	.long SYMBOL_NAME(sys_getsid)
	.long SYMBOL_NAME(sys_fdatasync)
	.long SYMBOL_NAME(sys_sysctl)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 150 sys_mlock */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_munlock */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_mlockall */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_munlockall */
	.long SYMBOL_NAME(sys_sched_setparam)
	.long SYMBOL_NAME(sys_sched_getparam)   /* 155 */
	.long SYMBOL_NAME(sys_sched_setscheduler)
	.long SYMBOL_NAME(sys_sched_getscheduler)
	.long SYMBOL_NAME(sys_sched_yield)
	.long SYMBOL_NAME(sys_sched_get_priority_max)
	.long SYMBOL_NAME(sys_sched_get_priority_min)  /* 160 */
	.long SYMBOL_NAME(sys_sched_rr_get_interval)
	.long SYMBOL_NAME(sys_nanosleep)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_mremap */
	.long SYMBOL_NAME(sys_setresuid16)
	.long SYMBOL_NAME(sys_getresuid16)	/* 165 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* for vm86 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_query_module */
	.long SYMBOL_NAME(sys_poll)
	.long SYMBOL_NAME(sys_nfsservctl)
	.long SYMBOL_NAME(sys_setresgid16)	/* 170 */
	.long SYMBOL_NAME(sys_getresgid16)
	.long SYMBOL_NAME(sys_prctl)
	.long SYMBOL_NAME(sys_rt_sigreturn)
	.long SYMBOL_NAME(sys_rt_sigaction)
	.long SYMBOL_NAME(sys_rt_sigprocmask)	/* 175 */
	.long SYMBOL_NAME(sys_rt_sigpending)
	.long SYMBOL_NAME(sys_rt_sigtimedwait)
	.long SYMBOL_NAME(sys_rt_sigqueueinfo)
	.long SYMBOL_NAME(sys_rt_sigsuspend)
	.long SYMBOL_NAME(sys_pread64)		/* 180 */
	.long SYMBOL_NAME(sys_pwrite64)
	.long SYMBOL_NAME(sys_lchown16);
	.long SYMBOL_NAME(sys_getcwd)
	.long SYMBOL_NAME(sys_capget)
	.long SYMBOL_NAME(sys_capset)           /* 185 */
	.long SYMBOL_NAME(sys_sigaltstack)
	.long SYMBOL_NAME(sys_sendfile)
	.long SYMBOL_NAME(sys_ni_syscall)	/* streams1 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* streams2 */
	.long SYMBOL_NAME(sys_vfork)            /* 190 */
	.long SYMBOL_NAME(sys_getrlimit)
	.long SYMBOL_NAME(sys_mmap_pgoff)
	.long SYMBOL_NAME(sys_truncate64)
	.long SYMBOL_NAME(sys_ftruncate64)
	.long SYMBOL_NAME(sys_stat64)		/* 195 */
	.long SYMBOL_NAME(sys_lstat64)
	.long SYMBOL_NAME(sys_fstat64)
	.long SYMBOL_NAME(sys_chown)
	.long SYMBOL_NAME(sys_getuid)
	.long SYMBOL_NAME(sys_getgid)		/* 200 */
	.long SYMBOL_NAME(sys_geteuid)
	.long SYMBOL_NAME(sys_getegid)
	.long SYMBOL_NAME(sys_setreuid)
	.long SYMBOL_NAME(sys_setregid)
	.long SYMBOL_NAME(sys_getgroups)	/* 205 */
	.long SYMBOL_NAME(sys_setgroups)
	.long SYMBOL_NAME(sys_fchown)
	.long SYMBOL_NAME(sys_setresuid)
	.long SYMBOL_NAME(sys_getresuid)
	.long SYMBOL_NAME(sys_setresgid)	/* 210 */
	.long SYMBOL_NAME(sys_getresgid)
	.long SYMBOL_NAME(sys_lchown)
	.long SYMBOL_NAME(sys_setuid)
	.long SYMBOL_NAME(sys_setgid)
	.long SYMBOL_NAME(sys_setfsuid)		/* 215 */
	.long SYMBOL_NAME(sys_setfsgid)
	.long SYMBOL_NAME(sys_pivot_root)
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_getdents64)	/* 220 */
	.long SYMBOL_NAME(sys_fcntl64)
	.long SYMBOL_NAME(sys_ni_syscall)	/* reserved TUX */
	.long SYMBOL_NAME(sys_ni_syscall)	/* reserved Security */
	.long SYMBOL_NAME(sys_gettid)
	.long SYMBOL_NAME(sys_readahead)	/* 225 */
	.long SYMBOL_NAME(sys_setxattr)
	.long SYMBOL_NAME(sys_lsetxattr)
	.long SYMBOL_NAME(sys_fsetxattr)
	.long SYMBOL_NAME(sys_getxattr)
	.long SYMBOL_NAME(sys_lgetxattr)	/* 230 */
	.long SYMBOL_NAME(sys_fgetxattr)
	.long SYMBOL_NAME(sys_listxattr)
	.long SYMBOL_NAME(sys_llistxattr)
	.long SYMBOL_NAME(sys_flistxattr)
	.long SYMBOL_NAME(sys_removexattr)	/* 235 */
	.long SYMBOL_NAME(sys_lremovexattr)
	.long SYMBOL_NAME(sys_fremovexattr)
	.long SYMBOL_NAME(sys_tkill)
	.long SYMBOL_NAME(sys_sendfile64)
	.long SYMBOL_NAME(sys_futex)		/* 240 */
	.long SYMBOL_NAME(sys_sched_setaffinity)
	.long SYMBOL_NAME(sys_sched_getaffinity)
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_io_setup)		/* 245 */
	.long SYMBOL_NAME(sys_io_destroy)
	.long SYMBOL_NAME(sys_io_getevents)
	.long SYMBOL_NAME(sys_io_submit)
	.long SYMBOL_NAME(sys_io_cancel)
	.long SYMBOL_NAME(sys_fadvise64)	/* 250 */
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_exit_group)
	.long SYMBOL_NAME(sys_lookup_dcookie)
	.long SYMBOL_NAME(sys_epoll_create)
	.long SYMBOL_NAME(sys_epoll_ctl)	/* 255 */
	.long SYMBOL_NAME(sys_epoll_wait)
 	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_remap_file_pages */
 	.long SYMBOL_NAME(sys_set_tid_address)
 	.long SYMBOL_NAME(sys_timer_create)
 	.long SYMBOL_NAME(sys_timer_settime)	/* 260 */
 	.long SYMBOL_NAME(sys_timer_gettime)
 	.long SYMBOL_NAME(sys_timer_getoverrun)
 	.long SYMBOL_NAME(sys_timer_delete)
 	.long SYMBOL_NAME(sys_clock_settime)
 	.long SYMBOL_NAME(sys_clock_gettime)	/* 265 */
 	.long SYMBOL_NAME(sys_clock_getres)
 	.long SYMBOL_NAME(sys_clock_nanosleep)
	.long SYMBOL_NAME(sys_statfs64)
	.long SYMBOL_NAME(sys_fstatfs64)	
	.long SYMBOL_NAME(sys_tgkill)		/* 270 */
	.long SYMBOL_NAME(sys_utimes)
 	.long SYMBOL_NAME(sys_fadvise64_64)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_vserver */
	.long SYMBOL_NAME(sys_ni_syscall)
	.long SYMBOL_NAME(sys_get_mempolicy)	/* 275 */
	.long SYMBOL_NAME(sys_set_mempolicy)
	.long SYMBOL_NAME(sys_mq_open)
	.long SYMBOL_NAME(sys_mq_unlink)
	.long SYMBOL_NAME(sys_mq_timedsend)
	.long SYMBOL_NAME(sys_mq_timedreceive)	/* 280 */
	.long SYMBOL_NAME(sys_mq_notify)
	.long SYMBOL_NAME(sys_mq_getsetattr)
	.long SYMBOL_NAME(sys_waitid)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_kexec_load */
	.long SYMBOL_NAME(sys_add_key) 		/* 285 */
	.long SYMBOL_NAME(sys_request_key)
	.long SYMBOL_NAME(sys_keyctl)
	.long SYMBOL_NAME(sys_ioprio_set)
	.long SYMBOL_NAME(sys_ioprio_get)	/* 290 */
	.long SYMBOL_NAME(sys_inotify_init)
	.long SYMBOL_NAME(sys_inotify_add_watch)
	.long SYMBOL_NAME(sys_inotify_rm_watch)
	.long SYMBOL_NAME(sys_migrate_pages)
	.long SYMBOL_NAME(sys_openat)		/* 295 */
	.long SYMBOL_NAME(sys_mkdirat)
	.long SYMBOL_NAME(sys_mknodat)
	.long SYMBOL_NAME(sys_fchownat)
	.long SYMBOL_NAME(sys_futimesat)
	.long SYMBOL_NAME(sys_fstatat64)	/* 300 */
	.long SYMBOL_NAME(sys_unlinkat)
	.long SYMBOL_NAME(sys_renameat)
	.long SYMBOL_NAME(sys_linkat)
	.long SYMBOL_NAME(sys_symlinkat)
	.long SYMBOL_NAME(sys_readlinkat)	/* 305 */
	.long SYMBOL_NAME(sys_fchmodat)
	.long SYMBOL_NAME(sys_faccessat)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_pselect6 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_ppoll */
	.long SYMBOL_NAME(sys_unshare)		/* 310 */
	.long SYMBOL_NAME(sys_set_robust_list)
	.long SYMBOL_NAME(sys_get_robust_list)
	.long SYMBOL_NAME(sys_splice)
	.long SYMBOL_NAME(sys_sync_file_range)
	.long SYMBOL_NAME(sys_tee)		/* 315 */
	.long SYMBOL_NAME(sys_vmsplice)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_move_pages */
	.long SYMBOL_NAME(sys_getcpu)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_epoll_pwait */

	.macro	call_sp addr
	mov.l	#SYMBOL_NAME(\addr),er6
	bra	SYMBOL_NAME(syscall_trampoline):8
	.endm

SYMBOL_NAME_LABEL(sys_clone)	
	call_sp	h8300_clone
	
SYMBOL_NAME_LABEL(sys_sigsuspend)
	call_sp	do_sigsuspend

SYMBOL_NAME_LABEL(sys_rt_sigsuspend)
	call_sp	do_rt_sigsuspend

SYMBOL_NAME_LABEL(sys_sigreturn)
	call_sp	do_sigreturn

SYMBOL_NAME_LABEL(sys_rt_sigreturn)
	call_sp	do_rt_sigreturn

SYMBOL_NAME_LABEL(sys_fork)
	call_sp	h8300_fork

SYMBOL_NAME_LABEL(sys_vfork)
	call_sp	h8300_vfork

SYMBOL_NAME_LABEL(syscall_trampoline)
	mov.l	sp,er0
	jmp	@er6
