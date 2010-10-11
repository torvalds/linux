/*
 * arch/alpha/kernel/systbls.S
 *
 * The system call table. 
 */

#include <asm/unistd.h>

	.data
	.align 3
	.globl sys_call_table
sys_call_table:
	.quad alpha_ni_syscall			/* 0 */
	.quad sys_exit
	.quad sys_fork
	.quad sys_read
	.quad sys_write
	.quad alpha_ni_syscall			/* 5 */
	.quad sys_close
	.quad sys_osf_wait4
	.quad alpha_ni_syscall
	.quad sys_link
	.quad sys_unlink			/* 10 */
	.quad alpha_ni_syscall
	.quad sys_chdir
	.quad sys_fchdir
	.quad sys_mknod
	.quad sys_chmod				/* 15 */
	.quad sys_chown
	.quad sys_osf_brk
	.quad alpha_ni_syscall
	.quad sys_lseek
	.quad sys_getxpid			/* 20 */
	.quad sys_osf_mount
	.quad sys_umount
	.quad sys_setuid
	.quad sys_getxuid
	.quad alpha_ni_syscall			/* 25 */
	.quad sys_ptrace
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 30 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_access
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 35 */
	.quad sys_sync
	.quad sys_kill
	.quad alpha_ni_syscall
	.quad sys_setpgid
	.quad alpha_ni_syscall			/* 40 */
	.quad sys_dup
	.quad sys_alpha_pipe
	.quad sys_osf_set_program_attributes
	.quad alpha_ni_syscall
	.quad sys_open				/* 45 */
	.quad alpha_ni_syscall
	.quad sys_getxgid
	.quad osf_sigprocmask
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 50 */
	.quad sys_acct
	.quad sys_sigpending
	.quad alpha_ni_syscall
	.quad sys_ioctl
	.quad alpha_ni_syscall			/* 55 */
	.quad alpha_ni_syscall
	.quad sys_symlink
	.quad sys_readlink
	.quad sys_execve
	.quad sys_umask				/* 60 */
	.quad sys_chroot
	.quad alpha_ni_syscall
	.quad sys_getpgrp
	.quad sys_getpagesize
	.quad alpha_ni_syscall			/* 65 */
	.quad sys_vfork
	.quad sys_newstat
	.quad sys_newlstat
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 70 */
	.quad sys_osf_mmap
	.quad alpha_ni_syscall
	.quad sys_munmap
	.quad sys_mprotect
	.quad sys_madvise			/* 75 */
	.quad sys_vhangup
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_getgroups
	/* map BSD's setpgrp to sys_setpgid for binary compatibility: */
	.quad sys_setgroups			/* 80 */
	.quad alpha_ni_syscall
	.quad sys_setpgid
	.quad sys_osf_setitimer
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 85 */
	.quad sys_osf_getitimer
	.quad sys_gethostname
	.quad sys_sethostname
	.quad sys_getdtablesize
	.quad sys_dup2				/* 90 */
	.quad sys_newfstat
	.quad sys_fcntl
	.quad sys_osf_select
	.quad sys_poll
	.quad sys_fsync				/* 95 */
	.quad sys_setpriority
	.quad sys_socket
	.quad sys_connect
	.quad sys_accept
	.quad osf_getpriority			/* 100 */
	.quad sys_send
	.quad sys_recv
	.quad sys_sigreturn
	.quad sys_bind
	.quad sys_setsockopt			/* 105 */
	.quad sys_listen
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 110 */
	.quad sys_sigsuspend
	.quad sys_osf_sigstack
	.quad sys_recvmsg
	.quad sys_sendmsg
	.quad alpha_ni_syscall			/* 115 */
	.quad sys_osf_gettimeofday
	.quad sys_osf_getrusage
	.quad sys_getsockopt
	.quad alpha_ni_syscall
#ifdef CONFIG_OSF4_COMPAT
	.quad sys_osf_readv			/* 120 */
	.quad sys_osf_writev
#else
	.quad sys_readv				/* 120 */
	.quad sys_writev
#endif
	.quad sys_osf_settimeofday
	.quad sys_fchown
	.quad sys_fchmod
	.quad sys_recvfrom			/* 125 */
	.quad sys_setreuid
	.quad sys_setregid
	.quad sys_rename
	.quad sys_truncate
	.quad sys_ftruncate			/* 130 */
	.quad sys_flock
	.quad sys_setgid
	.quad sys_sendto
	.quad sys_shutdown
	.quad sys_socketpair			/* 135 */
	.quad sys_mkdir
	.quad sys_rmdir
	.quad sys_osf_utimes
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 140 */
	.quad sys_getpeername
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_getrlimit
	.quad sys_setrlimit			/* 145 */
	.quad alpha_ni_syscall
	.quad sys_setsid
	.quad sys_quotactl
	.quad alpha_ni_syscall
	.quad sys_getsockname			/* 150 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 155 */
	.quad sys_osf_sigaction
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_osf_getdirentries
	.quad sys_osf_statfs			/* 160 */
	.quad sys_osf_fstatfs
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_osf_getdomainname		/* 165 */
	.quad sys_setdomainname
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 170 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 175 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 180 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 185 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 190 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 195 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	/* The OSF swapon has two extra arguments, but we ignore them.  */
	.quad sys_swapon
	.quad sys_msgctl			/* 200 */
	.quad sys_msgget
	.quad sys_msgrcv
	.quad sys_msgsnd
	.quad sys_semctl
	.quad sys_semget			/* 205 */
	.quad sys_semop
	.quad sys_osf_utsname
	.quad sys_lchown
	.quad sys_shmat
	.quad sys_shmctl			/* 210 */
	.quad sys_shmdt
	.quad sys_shmget
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 215 */
	.quad alpha_ni_syscall
	.quad sys_msync
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 220 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 225 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 230 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_getpgid
	.quad sys_getsid
	.quad sys_sigaltstack			/* 235 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 240 */
	.quad sys_osf_sysinfo
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_osf_proplist_syscall
	.quad alpha_ni_syscall			/* 245 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 250 */
	.quad sys_osf_usleep_thread
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad sys_sysfs
	.quad alpha_ni_syscall			/* 255 */
	.quad sys_osf_getsysinfo
	.quad sys_osf_setsysinfo
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 260 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 265 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 270 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 275 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 280 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 285 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 290 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall			/* 295 */
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
	.quad alpha_ni_syscall
/* linux-specific system calls start at 300 */
	.quad sys_bdflush			/* 300 */
	.quad sys_sethae
	.quad sys_mount
	.quad sys_old_adjtimex
	.quad sys_swapoff
	.quad sys_getdents			/* 305 */
	.quad sys_ni_syscall			/* 306: old create_module */
	.quad sys_init_module
	.quad sys_delete_module
	.quad sys_ni_syscall			/* 309: old get_kernel_syms */
	.quad sys_syslog			/* 310 */
	.quad sys_reboot
	.quad sys_clone
	.quad sys_uselib
	.quad sys_mlock
	.quad sys_munlock			/* 315 */
	.quad sys_mlockall
	.quad sys_munlockall
	.quad sys_sysinfo
	.quad sys_sysctl
	.quad sys_ni_syscall			/* 320 */
	.quad sys_oldumount
	.quad sys_swapon
	.quad sys_times
	.quad sys_personality
	.quad sys_setfsuid			/* 325 */
	.quad sys_setfsgid
	.quad sys_ustat
	.quad sys_statfs
	.quad sys_fstatfs
	.quad sys_sched_setparam		/* 330 */
	.quad sys_sched_getparam
	.quad sys_sched_setscheduler
	.quad sys_sched_getscheduler
	.quad sys_sched_yield
	.quad sys_sched_get_priority_max	/* 335 */
	.quad sys_sched_get_priority_min
	.quad sys_sched_rr_get_interval
	.quad sys_ni_syscall			/* sys_afs_syscall */
	.quad sys_newuname
	.quad sys_nanosleep			/* 340 */
	.quad sys_mremap
	.quad sys_nfsservctl
	.quad sys_setresuid
	.quad sys_getresuid
	.quad sys_pciconfig_read		/* 345 */
	.quad sys_pciconfig_write
	.quad sys_ni_syscall			/* 347: old query_module */
	.quad sys_prctl
	.quad sys_pread64
	.quad sys_pwrite64			/* 350 */
	.quad sys_rt_sigreturn
	.quad sys_rt_sigaction
	.quad sys_rt_sigprocmask
	.quad sys_rt_sigpending
	.quad sys_rt_sigtimedwait		/* 355 */
	.quad sys_rt_sigqueueinfo
	.quad sys_rt_sigsuspend
	.quad sys_select
	.quad sys_gettimeofday
	.quad sys_settimeofday			/* 360 */
	.quad sys_getitimer
	.quad sys_setitimer
	.quad sys_utimes
	.quad sys_getrusage
	.quad sys_wait4				/* 365 */
	.quad sys_adjtimex
	.quad sys_getcwd
	.quad sys_capget
	.quad sys_capset
	.quad sys_sendfile64			/* 370 */
	.quad sys_setresgid
	.quad sys_getresgid
	.quad sys_ni_syscall			/* sys_dipc */
	.quad sys_pivot_root
	.quad sys_mincore			/* 375 */
	.quad sys_pciconfig_iobase
	.quad sys_getdents64
	.quad sys_gettid
	.quad sys_readahead
	.quad sys_ni_syscall			/* 380 */
	.quad sys_tkill
	.quad sys_setxattr
	.quad sys_lsetxattr
	.quad sys_fsetxattr
	.quad sys_getxattr			/* 385 */
	.quad sys_lgetxattr
	.quad sys_fgetxattr
	.quad sys_listxattr
	.quad sys_llistxattr
	.quad sys_flistxattr			/* 390 */
	.quad sys_removexattr
	.quad sys_lremovexattr
	.quad sys_fremovexattr
	.quad sys_futex
	.quad sys_sched_setaffinity		/* 395 */
	.quad sys_sched_getaffinity
	.quad sys_ni_syscall			/* 397, tux */
	.quad sys_io_setup
	.quad sys_io_destroy
	.quad sys_io_getevents			/* 400 */
	.quad sys_io_submit
	.quad sys_io_cancel
	.quad sys_ni_syscall			/* 403, sys_alloc_hugepages */
	.quad sys_ni_syscall			/* 404, sys_free_hugepages */
	.quad sys_exit_group			/* 405 */
	.quad sys_lookup_dcookie
	.quad sys_epoll_create
	.quad sys_epoll_ctl
	.quad sys_epoll_wait
	.quad sys_remap_file_pages		/* 410 */
	.quad sys_set_tid_address
	.quad sys_restart_syscall
	.quad sys_fadvise64
	.quad sys_timer_create
	.quad sys_timer_settime			/* 415 */
	.quad sys_timer_gettime
	.quad sys_timer_getoverrun
	.quad sys_timer_delete
	.quad sys_clock_settime
	.quad sys_clock_gettime			/* 420 */
	.quad sys_clock_getres
	.quad sys_clock_nanosleep
	.quad sys_semtimedop
	.quad sys_tgkill
	.quad sys_stat64			/* 425 */
	.quad sys_lstat64
	.quad sys_fstat64
	.quad sys_ni_syscall			/* sys_vserver */
	.quad sys_ni_syscall			/* sys_mbind */
	.quad sys_ni_syscall			/* sys_get_mempolicy */
	.quad sys_ni_syscall			/* sys_set_mempolicy */
	.quad sys_mq_open
	.quad sys_mq_unlink
	.quad sys_mq_timedsend
	.quad sys_mq_timedreceive		/* 435 */
	.quad sys_mq_notify
	.quad sys_mq_getsetattr
	.quad sys_waitid
	.quad sys_add_key
	.quad sys_request_key			/* 440 */
	.quad sys_keyctl
	.quad sys_ioprio_set
	.quad sys_ioprio_get
	.quad sys_inotify_init
	.quad sys_inotify_add_watch		/* 445 */
	.quad sys_inotify_rm_watch
	.quad sys_fdatasync
	.quad sys_kexec_load
	.quad sys_migrate_pages
	.quad sys_openat			/* 450 */
	.quad sys_mkdirat
	.quad sys_mknodat
	.quad sys_fchownat
	.quad sys_futimesat
	.quad sys_fstatat64			/* 455 */
	.quad sys_unlinkat
	.quad sys_renameat
	.quad sys_linkat
	.quad sys_symlinkat
	.quad sys_readlinkat			/* 460 */
	.quad sys_fchmodat
	.quad sys_faccessat
	.quad sys_pselect6
	.quad sys_ppoll
	.quad sys_unshare			/* 465 */
	.quad sys_set_robust_list
	.quad sys_get_robust_list
	.quad sys_splice
	.quad sys_sync_file_range
	.quad sys_tee				/* 470 */
	.quad sys_vmsplice
	.quad sys_move_pages
	.quad sys_getcpu
	.quad sys_epoll_pwait
	.quad sys_utimensat			/* 475 */
	.quad sys_signalfd
	.quad sys_ni_syscall			/* sys_timerfd */
	.quad sys_eventfd
	.quad sys_recvmmsg
	.quad sys_fallocate				/* 480 */
	.quad sys_timerfd_create
	.quad sys_timerfd_settime
	.quad sys_timerfd_gettime
	.quad sys_signalfd4
	.quad sys_eventfd2				/* 485 */
	.quad sys_epoll_create1
	.quad sys_dup3
	.quad sys_pipe2
	.quad sys_inotify_init1
	.quad sys_preadv				/* 490 */
	.quad sys_pwritev
	.quad sys_rt_tgsigqueueinfo
	.quad sys_perf_event_open
	.quad sys_fanotify_init
	.quad sys_fanotify_mark				/* 495 */
	.quad sys_prlimit64

	.size sys_call_table, . - sys_call_table
	.type sys_call_table, @object

/* Remember to update everything, kids.  */
.ifne (. - sys_call_table) - (NR_SYSCALLS * 8)
.err
.endif
