#ifndef _ALPHA_UNISTD_H
#define _ALPHA_UNISTD_H

#define __NR_osf_syscall	  0	/* not implemented */
#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_osf_old_open	  5	/* not implemented */
#define __NR_close		  6
#define __NR_osf_wait4		  7
#define __NR_osf_old_creat	  8	/* not implemented */
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_osf_execve		 11	/* not implemented */
#define __NR_chdir		 12
#define __NR_fchdir		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
#define __NR_brk		 17
#define __NR_osf_getfsstat	 18	/* not implemented */
#define __NR_lseek		 19
#define __NR_getxpid		 20
#define __NR_osf_mount		 21
#define __NR_umount		 22
#define __NR_setuid		 23
#define __NR_getxuid		 24
#define __NR_exec_with_loader	 25	/* not implemented */
#define __NR_ptrace		 26
#define __NR_osf_nrecvmsg	 27	/* not implemented */
#define __NR_osf_nsendmsg	 28	/* not implemented */
#define __NR_osf_nrecvfrom	 29	/* not implemented */
#define __NR_osf_naccept	 30	/* not implemented */
#define __NR_osf_ngetpeername	 31	/* not implemented */
#define __NR_osf_ngetsockname	 32	/* not implemented */
#define __NR_access		 33
#define __NR_osf_chflags	 34	/* not implemented */
#define __NR_osf_fchflags	 35	/* not implemented */
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_osf_old_stat	 38	/* not implemented */
#define __NR_setpgid		 39
#define __NR_osf_old_lstat	 40	/* not implemented */
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_osf_set_program_attributes	43
#define __NR_osf_profil		 44	/* not implemented */
#define __NR_open		 45
#define __NR_osf_old_sigaction	 46	/* not implemented */
#define __NR_getxgid		 47
#define __NR_osf_sigprocmask	 48
#define __NR_osf_getlogin	 49	/* not implemented */
#define __NR_osf_setlogin	 50	/* not implemented */
#define __NR_acct		 51
#define __NR_sigpending		 52

#define __NR_ioctl		 54
#define __NR_osf_reboot		 55	/* not implemented */
#define __NR_osf_revoke		 56	/* not implemented */
#define __NR_symlink		 57
#define __NR_readlink		 58
#define __NR_execve		 59
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_osf_old_fstat	 62	/* not implemented */
#define __NR_getpgrp		 63
#define __NR_getpagesize	 64
#define __NR_osf_mremap		 65	/* not implemented */
#define __NR_vfork		 66
#define __NR_stat		 67
#define __NR_lstat		 68
#define __NR_osf_sbrk		 69	/* not implemented */
#define __NR_osf_sstk		 70	/* not implemented */
#define __NR_mmap		 71	/* OSF/1 mmap is superset of Linux */
#define __NR_osf_old_vadvise	 72	/* not implemented */
#define __NR_munmap		 73
#define __NR_mprotect		 74
#define __NR_madvise		 75
#define __NR_vhangup		 76
#define __NR_osf_kmodcall	 77	/* not implemented */
#define __NR_osf_mincore	 78	/* not implemented */
#define __NR_getgroups		 79
#define __NR_setgroups		 80
#define __NR_osf_old_getpgrp	 81	/* not implemented */
#define __NR_setpgrp		 82	/* BSD alias for setpgid */
#define __NR_osf_setitimer	 83
#define __NR_osf_old_wait	 84	/* not implemented */
#define __NR_osf_table		 85	/* not implemented */
#define __NR_osf_getitimer	 86
#define __NR_gethostname	 87
#define __NR_sethostname	 88
#define __NR_getdtablesize	 89
#define __NR_dup2		 90
#define __NR_fstat		 91
#define __NR_fcntl		 92
#define __NR_osf_select		 93
#define __NR_poll		 94
#define __NR_fsync		 95
#define __NR_setpriority	 96
#define __NR_socket		 97
#define __NR_connect		 98
#define __NR_accept		 99
#define __NR_getpriority	100
#define __NR_send		101
#define __NR_recv		102
#define __NR_sigreturn		103
#define __NR_bind		104
#define __NR_setsockopt		105
#define __NR_listen		106
#define __NR_osf_plock		107	/* not implemented */
#define __NR_osf_old_sigvec	108	/* not implemented */
#define __NR_osf_old_sigblock	109	/* not implemented */
#define __NR_osf_old_sigsetmask	110	/* not implemented */
#define __NR_sigsuspend		111
#define __NR_osf_sigstack	112
#define __NR_recvmsg		113
#define __NR_sendmsg		114
#define __NR_osf_old_vtrace	115	/* not implemented */
#define __NR_osf_gettimeofday	116
#define __NR_osf_getrusage	117
#define __NR_getsockopt		118

#define __NR_readv		120
#define __NR_writev		121
#define __NR_osf_settimeofday	122
#define __NR_fchown		123
#define __NR_fchmod		124
#define __NR_recvfrom		125
#define __NR_setreuid		126
#define __NR_setregid		127
#define __NR_rename		128
#define __NR_truncate		129
#define __NR_ftruncate		130
#define __NR_flock		131
#define __NR_setgid		132
#define __NR_sendto		133
#define __NR_shutdown		134
#define __NR_socketpair		135
#define __NR_mkdir		136
#define __NR_rmdir		137
#define __NR_osf_utimes		138
#define __NR_osf_old_sigreturn	139	/* not implemented */
#define __NR_osf_adjtime	140	/* not implemented */
#define __NR_getpeername	141
#define __NR_osf_gethostid	142	/* not implemented */
#define __NR_osf_sethostid	143	/* not implemented */
#define __NR_getrlimit		144
#define __NR_setrlimit		145
#define __NR_osf_old_killpg	146	/* not implemented */
#define __NR_setsid		147
#define __NR_quotactl		148
#define __NR_osf_oldquota	149	/* not implemented */
#define __NR_getsockname	150

#define __NR_osf_pid_block	153	/* not implemented */
#define __NR_osf_pid_unblock	154	/* not implemented */

#define __NR_sigaction		156
#define __NR_osf_sigwaitprim	157	/* not implemented */
#define __NR_osf_nfssvc		158	/* not implemented */
#define __NR_osf_getdirentries	159
#define __NR_osf_statfs		160
#define __NR_osf_fstatfs	161

#define __NR_osf_asynch_daemon	163	/* not implemented */
#define __NR_osf_getfh		164	/* not implemented */	
#define __NR_osf_getdomainname	165
#define __NR_setdomainname	166

#define __NR_osf_exportfs	169	/* not implemented */

#define __NR_osf_alt_plock	181	/* not implemented */

#define __NR_osf_getmnt		184	/* not implemented */

#define __NR_osf_alt_sigpending	187	/* not implemented */
#define __NR_osf_alt_setsid	188	/* not implemented */

#define __NR_osf_swapon		199
#define __NR_msgctl		200
#define __NR_msgget		201
#define __NR_msgrcv		202
#define __NR_msgsnd		203
#define __NR_semctl		204
#define __NR_semget		205
#define __NR_semop		206
#define __NR_osf_utsname	207
#define __NR_lchown		208
#define __NR_osf_shmat		209
#define __NR_shmctl		210
#define __NR_shmdt		211
#define __NR_shmget		212
#define __NR_osf_mvalid		213	/* not implemented */
#define __NR_osf_getaddressconf	214	/* not implemented */
#define __NR_osf_msleep		215	/* not implemented */
#define __NR_osf_mwakeup	216	/* not implemented */
#define __NR_msync		217
#define __NR_osf_signal		218	/* not implemented */
#define __NR_osf_utc_gettime	219	/* not implemented */
#define __NR_osf_utc_adjtime	220	/* not implemented */

#define __NR_osf_security	222	/* not implemented */
#define __NR_osf_kloadcall	223	/* not implemented */

#define __NR_getpgid		233
#define __NR_getsid		234
#define __NR_sigaltstack	235
#define __NR_osf_waitid		236	/* not implemented */
#define __NR_osf_priocntlset	237	/* not implemented */
#define __NR_osf_sigsendset	238	/* not implemented */
#define __NR_osf_set_speculative	239	/* not implemented */
#define __NR_osf_msfs_syscall	240	/* not implemented */
#define __NR_osf_sysinfo	241
#define __NR_osf_uadmin		242	/* not implemented */
#define __NR_osf_fuser		243	/* not implemented */
#define __NR_osf_proplist_syscall    244
#define __NR_osf_ntp_adjtime	245	/* not implemented */
#define __NR_osf_ntp_gettime	246	/* not implemented */
#define __NR_osf_pathconf	247	/* not implemented */
#define __NR_osf_fpathconf	248	/* not implemented */

#define __NR_osf_uswitch	250	/* not implemented */
#define __NR_osf_usleep_thread	251
#define __NR_osf_audcntl	252	/* not implemented */
#define __NR_osf_audgen		253	/* not implemented */
#define __NR_sysfs		254
#define __NR_osf_subsys_info	255	/* not implemented */
#define __NR_osf_getsysinfo	256
#define __NR_osf_setsysinfo	257
#define __NR_osf_afs_syscall	258	/* not implemented */
#define __NR_osf_swapctl	259	/* not implemented */
#define __NR_osf_memcntl	260	/* not implemented */
#define __NR_osf_fdatasync	261	/* not implemented */

/*
 * Ignore legacy syscalls that we don't use.
 */
#define __IGNORE_alarm
#define __IGNORE_creat
#define __IGNORE_getegid
#define __IGNORE_geteuid
#define __IGNORE_getgid
#define __IGNORE_getpid
#define __IGNORE_getppid
#define __IGNORE_getuid
#define __IGNORE_pause
#define __IGNORE_time
#define __IGNORE_utime
#define __IGNORE_umount2

/*
 * Linux-specific system calls begin at 300
 */
#define __NR_bdflush		300
#define __NR_sethae		301
#define __NR_mount		302
#define __NR_old_adjtimex	303
#define __NR_swapoff		304
#define __NR_getdents		305
#define __NR_create_module	306
#define __NR_init_module	307
#define __NR_delete_module	308
#define __NR_get_kernel_syms	309
#define __NR_syslog		310
#define __NR_reboot		311
#define __NR_clone		312
#define __NR_uselib		313
#define __NR_mlock		314
#define __NR_munlock		315
#define __NR_mlockall		316
#define __NR_munlockall		317
#define __NR_sysinfo		318
#define __NR__sysctl		319
/* 320 was sys_idle.  */
#define __NR_oldumount		321
#define __NR_swapon		322
#define __NR_times		323
#define __NR_personality	324
#define __NR_setfsuid		325
#define __NR_setfsgid		326
#define __NR_ustat		327
#define __NR_statfs		328
#define __NR_fstatfs		329
#define __NR_sched_setparam		330
#define __NR_sched_getparam		331
#define __NR_sched_setscheduler		332
#define __NR_sched_getscheduler		333
#define __NR_sched_yield		334
#define __NR_sched_get_priority_max	335
#define __NR_sched_get_priority_min	336
#define __NR_sched_rr_get_interval	337
#define __NR_afs_syscall		338
#define __NR_uname			339
#define __NR_nanosleep			340
#define __NR_mremap			341
#define __NR_nfsservctl			342
#define __NR_setresuid			343
#define __NR_getresuid			344
#define __NR_pciconfig_read		345
#define __NR_pciconfig_write		346
#define __NR_query_module		347
#define __NR_prctl			348
#define __NR_pread64			349
#define __NR_pwrite64			350
#define __NR_rt_sigreturn		351
#define __NR_rt_sigaction		352
#define __NR_rt_sigprocmask		353
#define __NR_rt_sigpending		354
#define __NR_rt_sigtimedwait		355
#define __NR_rt_sigqueueinfo		356
#define __NR_rt_sigsuspend		357
#define __NR_select			358
#define __NR_gettimeofday		359
#define __NR_settimeofday		360
#define __NR_getitimer			361
#define __NR_setitimer			362
#define __NR_utimes			363
#define __NR_getrusage			364
#define __NR_wait4			365
#define __NR_adjtimex			366
#define __NR_getcwd			367
#define __NR_capget			368
#define __NR_capset			369
#define __NR_sendfile			370
#define __NR_setresgid			371
#define __NR_getresgid			372
#define __NR_dipc			373
#define __NR_pivot_root			374
#define __NR_mincore			375
#define __NR_pciconfig_iobase		376
#define __NR_getdents64			377
#define __NR_gettid			378
#define __NR_readahead			379
/* 380 is unused */
#define __NR_tkill			381
#define __NR_setxattr			382
#define __NR_lsetxattr			383
#define __NR_fsetxattr			384
#define __NR_getxattr			385
#define __NR_lgetxattr			386
#define __NR_fgetxattr			387
#define __NR_listxattr			388
#define __NR_llistxattr			389
#define __NR_flistxattr			390
#define __NR_removexattr		391
#define __NR_lremovexattr		392
#define __NR_fremovexattr		393
#define __NR_futex			394
#define __NR_sched_setaffinity		395     
#define __NR_sched_getaffinity		396
#define __NR_tuxcall			397
#define __NR_io_setup			398
#define __NR_io_destroy			399
#define __NR_io_getevents		400
#define __NR_io_submit			401
#define __NR_io_cancel			402
#define __NR_exit_group			405
#define __NR_lookup_dcookie		406
#define __NR_epoll_create		407
#define __NR_epoll_ctl			408
#define __NR_epoll_wait			409
/* Feb 2007: These three sys_epoll defines shouldn't be here but culling
 * them would break userspace apps ... we'll kill them off in 2010 :) */
#define __NR_sys_epoll_create		__NR_epoll_create
#define __NR_sys_epoll_ctl		__NR_epoll_ctl
#define __NR_sys_epoll_wait		__NR_epoll_wait
#define __NR_remap_file_pages		410
#define __NR_set_tid_address		411
#define __NR_restart_syscall		412
#define __NR_fadvise64			413
#define __NR_timer_create		414
#define __NR_timer_settime		415
#define __NR_timer_gettime		416
#define __NR_timer_getoverrun		417
#define __NR_timer_delete		418
#define __NR_clock_settime		419
#define __NR_clock_gettime		420
#define __NR_clock_getres		421
#define __NR_clock_nanosleep		422
#define __NR_semtimedop			423
#define __NR_tgkill			424
#define __NR_stat64			425
#define __NR_lstat64			426
#define __NR_fstat64			427
#define __NR_vserver			428
#define __NR_mbind			429
#define __NR_get_mempolicy		430
#define __NR_set_mempolicy		431
#define __NR_mq_open			432
#define __NR_mq_unlink			433
#define __NR_mq_timedsend		434
#define __NR_mq_timedreceive		435
#define __NR_mq_notify			436
#define __NR_mq_getsetattr		437
#define __NR_waitid			438
#define __NR_add_key			439
#define __NR_request_key		440
#define __NR_keyctl			441
#define __NR_ioprio_set			442
#define __NR_ioprio_get			443
#define __NR_inotify_init		444
#define __NR_inotify_add_watch		445
#define __NR_inotify_rm_watch		446
#define __NR_fdatasync			447
#define __NR_kexec_load			448
#define __NR_migrate_pages		449
#define __NR_openat			450
#define __NR_mkdirat			451
#define __NR_mknodat			452
#define __NR_fchownat			453
#define __NR_futimesat			454
#define __NR_fstatat64			455
#define __NR_unlinkat			456
#define __NR_renameat			457
#define __NR_linkat			458
#define __NR_symlinkat			459
#define __NR_readlinkat			460
#define __NR_fchmodat			461
#define __NR_faccessat			462
#define __NR_pselect6			463
#define __NR_ppoll			464
#define __NR_unshare			465
#define __NR_set_robust_list		466
#define __NR_get_robust_list		467
#define __NR_splice			468
#define __NR_sync_file_range		469
#define __NR_tee			470
#define __NR_vmsplice			471
#define __NR_move_pages			472
#define __NR_getcpu			473
#define __NR_epoll_pwait		474
#define __NR_utimensat			475
#define __NR_signalfd			476
#define __NR_timerfd			477
#define __NR_eventfd			478
#define __NR_recvmmsg			479
#define __NR_fallocate			480
#define __NR_timerfd_create		481
#define __NR_timerfd_settime		482
#define __NR_timerfd_gettime		483
#define __NR_signalfd4			484
#define __NR_eventfd2			485
#define __NR_epoll_create1		486
#define __NR_dup3			487
#define __NR_pipe2			488
#define __NR_inotify_init1		489
#define __NR_preadv			490
#define __NR_pwritev			491
#define __NR_rt_tgsigqueueinfo		492
#define __NR_perf_event_open		493
#define __NR_fanotify_init		494
#define __NR_fanotify_mark		495
#define __NR_prlimit64			496
#define __NR_name_to_handle_at		497
#define __NR_open_by_handle_at		498
#define __NR_clock_adjtime		499
#define __NR_syncfs			500

#ifdef __KERNEL__

#define NR_SYSCALLS			501

#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_OLD_READDIR
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_OLD_GETRLIMIT
#define __ARCH_WANT_SYS_OLDUMOUNT
#define __ARCH_WANT_SYS_SIGPENDING
#define __ARCH_WANT_SYS_RT_SIGSUSPEND

/* "Conditional" syscalls.  What we want is

	__attribute__((weak,alias("sys_ni_syscall")))

   but that raises the problem of what type to give the symbol.  If we use
   a prototype, it'll conflict with the definition given in this file and
   others.  If we use __typeof, we discover that not all symbols actually
   have declarations.  If we use no prototype, then we get warnings from
   -Wstrict-prototypes.  Ho hum.  */

#define cond_syscall(x)  asm(".weak\t" #x "\n" #x " = sys_ni_syscall")

#endif /* __KERNEL__ */
#endif /* _ALPHA_UNISTD_H */
