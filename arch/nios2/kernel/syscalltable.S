/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * Derived from m68knommu
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/unistd.h>
#include <asm/asm-macros.h>

.text
ALIGN
ENTRY(sys_call_table)
	.long sys_restart_syscall /* 0 */
	.long sys_exit
	.long __sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_chown
	.long sys_ni_syscall	/* old sys_break */
	.long sys_ni_syscall	/* old sys_stat */
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_ni_syscall	/* old sys_fstat */
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall	/* old sys_stty */
	.long sys_ni_syscall	/* old sys_gtty */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35: old sys_ftime */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall	/* old sys_prof */
	.long sys_brk		/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_ni_syscall	/* old sys_signal */
	.long sys_geteuid
	.long sys_getegid	/* 50 */
	.long sys_acct
	.long sys_umount
	.long sys_ni_syscall	/* old sys_lock */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall	/* old sys_mpx */
	.long sys_setpgid
	.long sys_ni_syscall	/* old sys_ulimit */
	.long sys_ni_syscall	/* old sys_olduname */
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_ni_syscall	/* old sys_sgetmask */
	.long sys_ni_syscall	/* old sys_ssetmask */
	.long sys_setreuid	/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_ni_syscall	/* old sys_getrlimit */
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups	/* 80 */
	.long sys_setgroups
	.long sys_old_select
	.long sys_symlink
	.long sys_ni_syscall	/* old sys_lstat */
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_ni_syscall	/* old sys_readdir */
	.long sys_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall	/* old sys_profil */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ni_syscall	/* old sys_ioperm */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall	/* old sys_uname */
	.long sys_ni_syscall	/* old sys_iopl */
	.long sys_vhangup
	.long sys_ni_syscall	/* old sys_idle */
	.long sys_nios2cmpxchg	/* nios2-specific compare and exchange syscall
					for atomic operations */
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long __sys_clone	/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_cacheflush	/* modify_ldt for i386 */
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_ni_syscall	/* old sys_create_module */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall	/* 130: old sys_get_kernel_syms */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* old sys_afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek	/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam /* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid	/* 165 */
	.long sys_getpagesize
	.long sys_ni_syscall	/* old sys_query_module */
	.long sys_poll
	.long sys_ni_syscall	/* old sys_nfsservctl */
	.long sys_setresgid	/* 170 */
	.long sys_getresgid
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask /* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64	/* 180 */
	.long sys_pwrite64
	.long sys_lchown
	.long sys_getcwd
	.long sys_capget
	.long sys_capset	/* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall	/* old sys_getpmsg (streams1) */
	.long sys_ni_syscall	/* old sys_putpmsg (streams2) */
	.long __sys_vfork	/* 190 */
	.long sys_getrlimit
	.long sys_mmap_pgoff
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64	/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_chown
	.long sys_getuid
	.long sys_getgid	/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups	/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid	/* 210 */
	.long sys_getresgid
	.long sys_lchown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid	/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_ni_syscall	/* unused */
	.long sys_ni_syscall	/* unused */
	.long sys_getdents64	/* 220 */
	.long sys_gettid
	.long sys_tkill
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr	/* 225 */
	.long sys_getxattr
	.long sys_lgetxattr
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr	/* 230 */
	.long sys_flistxattr
	.long sys_removexattr
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_futex		/* 235 */
	.long sys_sendfile64
	.long sys_mincore
	.long sys_madvise
	.long sys_fcntl64
	.long sys_readahead	/* 240 */
	.long sys_io_setup
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit
	.long sys_io_cancel	/* 245 */
	.long sys_fadvise64
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl	/* 250 */
	.long sys_epoll_wait
	.long sys_remap_file_pages
	.long sys_set_tid_address
	.long sys_timer_create
	.long sys_timer_settime	/* 255 */
	.long sys_timer_gettime
	.long sys_timer_getoverrun
	.long sys_timer_delete
	.long sys_clock_settime
	.long sys_clock_gettime	/* 260 */
	.long sys_clock_getres
	.long sys_clock_nanosleep
	.long sys_statfs64
	.long sys_fstatfs64
	.long sys_tgkill	/* 265 */
	.long sys_utimes
	.long sys_fadvise64_64
	.long sys_mbind
	.long sys_get_mempolicy
	.long sys_set_mempolicy	/* 270 */
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive
	.long sys_mq_notify	/* 275 */
	.long sys_mq_getsetattr
	.long sys_waitid
	.long sys_ni_syscall	/* old sys_setaltroot */
	.long sys_add_key
	.long sys_request_key	/* 280 */
	.long sys_keyctl
	.long sys_ioprio_set
	.long sys_ioprio_get
	.long sys_inotify_init
	.long sys_inotify_add_watch	/* 285 */
	.long sys_inotify_rm_watch
	.long sys_migrate_pages
	.long sys_openat
	.long sys_mkdirat
	.long sys_mknodat		/* 290 */
	.long sys_fchownat
	.long sys_futimesat
	.long sys_fstatat64
	.long sys_unlinkat
	.long sys_renameat		/* 295 */
	.long sys_linkat
	.long sys_symlinkat
	.long sys_readlinkat
	.long sys_fchmodat
	.long sys_faccessat		/* 300 */
	.long sys_pselect6
	.long sys_ppoll
	.long sys_unshare
	.long sys_set_robust_list
	.long sys_get_robust_list	/* 305 */
	.long sys_splice
	.long sys_sync_file_range
	.long sys_tee
	.long sys_vmsplice
	.long sys_move_pages		/* 310 */
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_kexec_load
	.long sys_getcpu
	.long sys_epoll_pwait		/* 315 */
	.long sys_utimensat
	.long sys_signalfd
	.long sys_timerfd_create
	.long sys_eventfd
	.long sys_pread64		/* 320 */
	.long sys_pwrite64
	.long sys_fallocate
	.long sys_timerfd_settime
	.long sys_timerfd_gettime
	.long sys_signalfd4		/* 325 */
	.long sys_eventfd2
	.long sys_epoll_create1
	.long sys_dup3
	.long sys_pipe2
	.long sys_inotify_init1		/* 330 */
	.long sys_preadv
	.long sys_pwritev
	.long sys_rt_sigqueueinfo
	.long sys_perf_event_open
	.long sys_recvmmsg		/* 335 */
	.long sys_fanotify_init
	.long sys_fanotify_mark
	.long sys_prlimit64
	.long sys_name_to_handle_at
	.long sys_open_by_handle_at	/* 340 */
	.long sys_clock_adjtime
	.long sys_syncfs
	.long sys_sendmmsg
	.long sys_setns
	.long sys_process_vm_readv	/* 345 */
	.long sys_process_vm_writev
	.long sys_kcmp
	.long sys_finit_module

	.rept NR_syscalls - 349
		.long sys_ni_syscall
	.endr
