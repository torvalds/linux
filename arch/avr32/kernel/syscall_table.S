/*
 * AVR32 system call table
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

	.section .rodata,"a",@progbits
	.type sys_call_table,@object
	.global sys_call_table
	.align 2
sys_call_table:
	.long sys_restart_syscall
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open
	.long sys_close
	.long sys_umask
	.long sys_creat
	.long sys_link
	.long sys_unlink /* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod
	.long sys_chown
	.long sys_lchown
	.long sys_lseek
	.long sys_llseek
	.long sys_getpid /* 20 */
	.long sys_mount
	.long sys_umount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime
	.long sys_ptrace
	.long sys_alarm
	.long sys_pause
	.long sys_utime
	.long sys_newstat /* 30 */
	.long sys_newfstat
	.long sys_newlstat
	.long sys_access
	.long sys_chroot
	.long sys_sync
	.long sys_fsync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir /* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_clone
	.long sys_brk
	.long sys_setgid
	.long sys_getgid
	.long sys_getcwd
	.long sys_geteuid
	.long sys_getegid /* 50 */
	.long sys_acct
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_ioctl
	.long sys_fcntl
	.long sys_setpgid
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setreuid /* 60 */
	.long sys_setregid
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp
	.long sys_setsid
	.long sys_rt_sigaction
	.long __sys_rt_sigreturn
	.long sys_rt_sigprocmask
	.long sys_rt_sigpending /* 70 */
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long __sys_rt_sigsuspend
	.long sys_sethostname
	.long sys_setrlimit
	.long sys_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups /* 80 */
	.long sys_setgroups
	.long sys_select
	.long sys_symlink
	.long sys_fchdir
	.long sys_readlink
	.long sys_pread64
	.long sys_pwrite64
	.long sys_swapon
	.long sys_reboot
	.long __sys_mmap2 /* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown
	.long sys_getpriority
	.long sys_setpriority
	.long sys_wait4
	.long sys_statfs
	.long sys_fstatfs /* 100 */
	.long sys_vhangup
	.long sys_sigaltstack
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer
	.long sys_swapoff
	.long sys_sysinfo
	.long sys_ni_syscall /* was sys_ipc briefly */
	.long sys_sendfile
	.long sys_setdomainname /* 110 */
	.long sys_newuname
	.long sys_adjtimex
	.long sys_mprotect
	.long sys_vfork
	.long sys_init_module
	.long sys_delete_module
	.long sys_quotactl
	.long sys_getpgid
	.long sys_bdflush
	.long sys_sysfs /* 120 */
	.long sys_personality
	.long sys_ni_syscall /* reserved for afs_syscall */
	.long sys_getdents
	.long sys_flock
	.long sys_msync
	.long sys_readv
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl /* 130 */
	.long sys_mlock
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max  /* 140 */
	.long sys_sched_get_priority_min
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_poll
	.long sys_ni_syscall /* 145 was nfsservctl */
	.long sys_setresgid
	.long sys_getresgid
	.long sys_prctl
	.long sys_socket
	.long sys_bind /* 150 */
	.long sys_connect
	.long sys_listen
	.long sys_accept
	.long sys_getsockname
	.long sys_getpeername
	.long sys_socketpair
	.long sys_send
	.long sys_recv
	.long __sys_sendto
	.long __sys_recvfrom /* 160 */
	.long sys_shutdown
	.long sys_setsockopt
	.long sys_getsockopt
	.long sys_sendmsg
	.long sys_recvmsg
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64
	.long sys_lstat64
	.long sys_fstat64 /* 170 */
	.long sys_pivot_root
	.long sys_mincore
	.long sys_madvise
	.long sys_getdents64
	.long sys_fcntl64
	.long sys_gettid
	.long sys_readahead
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr /* 180 */
	.long sys_getxattr
	.long sys_lgetxattr
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr
	.long sys_lremovexattr
	.long sys_fremovexattr
	.long sys_tkill /* 190 */
	.long sys_sendfile64
	.long sys_futex
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_capget
	.long sys_capset
	.long sys_io_setup
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit /* 200 */
	.long sys_io_cancel
	.long sys_fadvise64
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl
	.long sys_epoll_wait
	.long sys_remap_file_pages
	.long sys_set_tid_address
	.long sys_timer_create /* 210 */
	.long sys_timer_settime
	.long sys_timer_gettime
	.long sys_timer_getoverrun
	.long sys_timer_delete
	.long sys_clock_settime
	.long sys_clock_gettime
	.long sys_clock_getres
	.long sys_clock_nanosleep
	.long sys_statfs64
	.long sys_fstatfs64 /* 220 */
	.long sys_tgkill
	.long sys_ni_syscall /* reserved for TUX */
	.long sys_utimes
	.long sys_fadvise64_64
	.long sys_cacheflush
	.long sys_ni_syscall /* sys_vserver */
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive /* 230 */
	.long sys_mq_notify
	.long sys_mq_getsetattr
	.long sys_kexec_load
	.long sys_waitid
	.long sys_add_key
	.long sys_request_key
	.long sys_keyctl
	.long sys_ioprio_set
	.long sys_ioprio_get
	.long sys_inotify_init /* 240 */
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
	.long sys_openat
	.long sys_mkdirat
	.long sys_mknodat
	.long sys_fchownat
	.long sys_futimesat
	.long sys_fstatat64
	.long sys_unlinkat
	.long sys_renameat /* 250 */
	.long sys_linkat
	.long sys_symlinkat
	.long sys_readlinkat
	.long sys_fchmodat
	.long sys_faccessat
	.long __sys_pselect6
	.long sys_ppoll
	.long sys_unshare
	.long sys_set_robust_list
	.long sys_get_robust_list /* 260 */
	.long __sys_splice
	.long __sys_sync_file_range
	.long sys_tee
	.long sys_vmsplice
	.long __sys_epoll_pwait
	.long sys_msgget
	.long sys_msgsnd
	.long sys_msgrcv
	.long sys_msgctl
	.long sys_semget /* 270 */
	.long sys_semop
	.long sys_semctl
	.long sys_semtimedop
	.long sys_shmat
	.long sys_shmget
	.long sys_shmdt
	.long sys_shmctl
	.long sys_utimensat
	.long sys_signalfd
	.long sys_ni_syscall /* 280, was sys_timerfd */
	.long sys_eventfd
	.long sys_ni_syscall /* 282, was half-implemented recvmmsg */
	.long sys_setns
	.long sys_pread64
	.long sys_pwrite64
	.long sys_timerfd_create
	.long __sys_fallocate
	.long sys_timerfd_settime
	.long sys_timerfd_gettime
	.long sys_signalfd4 /* 290 */
	.long sys_eventfd2
	.long sys_epoll_create1
	.long sys_dup3
	.long sys_pipe2
	.long sys_inotify_init1
	.long sys_preadv
	.long sys_pwritev
	.long sys_rt_tgsigqueueinfo
	.long sys_perf_event_open
	.long sys_recvmmsg /* 300 */
	.long sys_fanotify_init
	.long __sys_fanotify_mark
	.long sys_prlimit64
	.long sys_name_to_handle_at
	.long sys_open_by_handle_at
	.long sys_clock_adjtime
	.long sys_syncfs
	.long sys_sendmmsg
	.long __sys_process_vm_readv
	.long __sys_process_vm_writev /* 310 */
	.long sys_kcmp
	.long sys_finit_module
	.long sys_sched_setattr
	.long sys_sched_getattr
	.long sys_renameat2
	.long sys_seccomp
	.long sys_getrandom
	.long sys_memfd_create
	.long sys_bpf
	.long sys_execveat /* 320 */
	.long sys_accept4
	.long sys_userfaultfd
	.long sys_membarrier
	.long sys_mlock2
	.long __sys_copy_file_range
	.long __sys_preadv2
	.long __sys_pwritev2
	.long sys_ni_syscall /* r8 is saturated at nr_syscalls */
