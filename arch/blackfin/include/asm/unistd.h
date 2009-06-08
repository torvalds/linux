#ifndef __ASM_BFIN_UNISTD_H
#define __ASM_BFIN_UNISTD_H
/*
 * This file contains the system call numbers.
 */
#define __NR_restart_syscall	  0
#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_open		  5
#define __NR_close		  6
				/* 7 __NR_waitpid obsolete */
#define __NR_creat		  8
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_execve		 11
#define __NR_chdir		 12
#define __NR_time		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_chown		 16
				/* 17 __NR_break obsolete */
				/* 18 __NR_oldstat obsolete */
#define __NR_lseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
				/* 22 __NR_umount obsolete */
#define __NR_setuid		 23
#define __NR_getuid		 24
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
				/* 28 __NR_oldfstat obsolete */
#define __NR_pause		 29
				/* 30 __NR_utime obsolete */
				/* 31 __NR_stty obsolete */
				/* 32 __NR_gtty obsolete */
#define __NR_access		 33
#define __NR_nice		 34
				/* 35 __NR_ftime obsolete */
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
				/* 44 __NR_prof obsolete */
#define __NR_brk		 45
#define __NR_setgid		 46
#define __NR_getgid		 47
				/* 48 __NR_signal obsolete */
#define __NR_geteuid		 49
#define __NR_getegid		 50
#define __NR_acct		 51
#define __NR_umount2		 52
				/* 53 __NR_lock obsolete */
#define __NR_ioctl		 54
#define __NR_fcntl		 55
				/* 56 __NR_mpx obsolete */
#define __NR_setpgid		 57
				/* 58 __NR_ulimit obsolete */
				/* 59 __NR_oldolduname obsolete */
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
				/* 67 __NR_sigaction obsolete */
#define __NR_sgetmask		 68
#define __NR_ssetmask		 69
#define __NR_setreuid		 70
#define __NR_setregid		 71
				/* 72 __NR_sigsuspend obsolete */
				/* 73 __NR_sigpending obsolete */
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
				/* 76 __NR_old_getrlimit obsolete */
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
#define __NR_getgroups		 80
#define __NR_setgroups		 81
				/* 82 __NR_select obsolete */
#define __NR_symlink		 83
				/* 84 __NR_oldlstat obsolete */
#define __NR_readlink		 85
				/* 86 __NR_uselib obsolete */
				/* 87 __NR_swapon obsolete */
#define __NR_reboot		 88
				/* 89 __NR_readdir obsolete */
				/* 90 __NR_mmap obsolete */
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
#define __NR_fchown		 95
#define __NR_getpriority	 96
#define __NR_setpriority	 97
				/* 98 __NR_profil obsolete */
#define __NR_statfs		 99
#define __NR_fstatfs		100
				/* 101 __NR_ioperm */
				/* 102 __NR_socketcall obsolete */
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_stat		106
#define __NR_lstat		107
#define __NR_fstat		108
				/* 109 __NR_olduname obsolete */
				/* 110 __NR_iopl obsolete */
#define __NR_vhangup		111
				/* 112 __NR_idle obsolete */
				/* 113 __NR_vm86old */
#define __NR_wait4		114
				/* 115 __NR_swapoff obsolete */
#define __NR_sysinfo		116
				/* 117 __NR_ipc oboslete */
#define __NR_fsync		118
				/* 119 __NR_sigreturn obsolete */
#define __NR_clone		120
#define __NR_setdomainname	121
#define __NR_uname		122
				/* 123 __NR_modify_ldt obsolete */
#define __NR_adjtimex		124
#define __NR_mprotect		125
				/* 126 __NR_sigprocmask obsolete */
				/* 127 __NR_create_module obsolete */
#define __NR_init_module	128
#define __NR_delete_module	129
				/* 130 __NR_get_kernel_syms obsolete */
#define __NR_quotactl		131
#define __NR_getpgid		132
#define __NR_fchdir		133
#define __NR_bdflush		134
				/* 135 was sysfs */
#define __NR_personality	136
				/* 137 __NR_afs_syscall */
#define __NR_setfsuid		138
#define __NR_setfsgid		139
#define __NR__llseek		140
#define __NR_getdents		141
				/* 142 __NR__newselect obsolete */
#define __NR_flock		143
				/* 144 __NR_msync obsolete */
#define __NR_readv		145
#define __NR_writev		146
#define __NR_getsid		147
#define __NR_fdatasync		148
#define __NR__sysctl		149
				/* 150 __NR_mlock */
				/* 151 __NR_munlock */
				/* 152 __NR_mlockall */
				/* 153 __NR_munlockall */
#define __NR_sched_setparam		154
#define __NR_sched_getparam		155
#define __NR_sched_setscheduler		156
#define __NR_sched_getscheduler		157
#define __NR_sched_yield		158
#define __NR_sched_get_priority_max	159
#define __NR_sched_get_priority_min	160
#define __NR_sched_rr_get_interval	161
#define __NR_nanosleep		162
#define __NR_mremap		163
#define __NR_setresuid		164
#define __NR_getresuid		165
				/* 166 __NR_vm86 */
				/* 167 __NR_query_module */
				/* 168 __NR_poll */
#define __NR_nfsservctl		169
#define __NR_setresgid		170
#define __NR_getresgid		171
#define __NR_prctl		172
#define __NR_rt_sigreturn	173
#define __NR_rt_sigaction	174
#define __NR_rt_sigprocmask	175
#define __NR_rt_sigpending	176
#define __NR_rt_sigtimedwait	177
#define __NR_rt_sigqueueinfo	178
#define __NR_rt_sigsuspend	179
#define __NR_pread		180
#define __NR_pwrite		181
#define __NR_lchown		182
#define __NR_getcwd		183
#define __NR_capget		184
#define __NR_capset		185
#define __NR_sigaltstack	186
#define __NR_sendfile		187
				/* 188 __NR_getpmsg */
				/* 189 __NR_putpmsg */
#define __NR_vfork		190
#define __NR_getrlimit		191
#define __NR_mmap2		192
#define __NR_truncate64		193
#define __NR_ftruncate64	194
#define __NR_stat64		195
#define __NR_lstat64		196
#define __NR_fstat64		197
#define __NR_chown32		198
#define __NR_getuid32		199
#define __NR_getgid32		200
#define __NR_geteuid32		201
#define __NR_getegid32		202
#define __NR_setreuid32		203
#define __NR_setregid32		204
#define __NR_getgroups32	205
#define __NR_setgroups32	206
#define __NR_fchown32		207
#define __NR_setresuid32	208
#define __NR_getresuid32	209
#define __NR_setresgid32	210
#define __NR_getresgid32	211
#define __NR_lchown32		212
#define __NR_setuid32		213
#define __NR_setgid32		214
#define __NR_setfsuid32		215
#define __NR_setfsgid32		216
#define __NR_pivot_root		217
				/* 218 __NR_mincore */
				/* 219 __NR_madvise */
#define __NR_getdents64		220
#define __NR_fcntl64		221
				/* 222 reserved for TUX */
				/* 223 reserved for TUX */
#define __NR_gettid		224
#define __NR_readahead		225
#define __NR_setxattr		226
#define __NR_lsetxattr		227
#define __NR_fsetxattr		228
#define __NR_getxattr		229
#define __NR_lgetxattr		230
#define __NR_fgetxattr		231
#define __NR_listxattr		232
#define __NR_llistxattr		233
#define __NR_flistxattr		234
#define __NR_removexattr	235
#define __NR_lremovexattr	236
#define __NR_fremovexattr	237
#define __NR_tkill		238
#define __NR_sendfile64		239
#define __NR_futex		240
#define __NR_sched_setaffinity	241
#define __NR_sched_getaffinity	242
				/* 243 __NR_set_thread_area */
				/* 244 __NR_get_thread_area */
#define __NR_io_setup		245
#define __NR_io_destroy		246
#define __NR_io_getevents	247
#define __NR_io_submit		248
#define __NR_io_cancel		249
				/* 250 __NR_alloc_hugepages */
				/* 251 __NR_free_hugepages */
#define __NR_exit_group		252
#define __NR_lookup_dcookie     253
#define __NR_bfin_spinlock      254

#define __NR_epoll_create	255
#define __NR_epoll_ctl		256
#define __NR_epoll_wait		257
				/* 258 __NR_remap_file_pages */
#define __NR_set_tid_address	259
#define __NR_timer_create	260
#define __NR_timer_settime	261
#define __NR_timer_gettime	262
#define __NR_timer_getoverrun	263
#define __NR_timer_delete	264
#define __NR_clock_settime	265
#define __NR_clock_gettime	266
#define __NR_clock_getres	267
#define __NR_clock_nanosleep	268
#define __NR_statfs64		269
#define __NR_fstatfs64		270
#define __NR_tgkill		271
#define __NR_utimes		272
#define __NR_fadvise64_64	273
				/* 274 __NR_vserver */
				/* 275 __NR_mbind */
				/* 276 __NR_get_mempolicy */
				/* 277 __NR_set_mempolicy */
#define __NR_mq_open 		278
#define __NR_mq_unlink		279
#define __NR_mq_timedsend	280
#define __NR_mq_timedreceive	281
#define __NR_mq_notify		282
#define __NR_mq_getsetattr	283
#define __NR_kexec_load		284
#define __NR_waitid		285
#define __NR_add_key		286
#define __NR_request_key	287
#define __NR_keyctl		288
#define __NR_ioprio_set		289
#define __NR_ioprio_get		290
#define __NR_inotify_init	291
#define __NR_inotify_add_watch	292
#define __NR_inotify_rm_watch	293
				/* 294 __NR_migrate_pages */
#define __NR_openat		295
#define __NR_mkdirat		296
#define __NR_mknodat		297
#define __NR_fchownat		298
#define __NR_futimesat		299
#define __NR_fstatat64		300
#define __NR_unlinkat		301
#define __NR_renameat		302
#define __NR_linkat		303
#define __NR_symlinkat		304
#define __NR_readlinkat		305
#define __NR_fchmodat		306
#define __NR_faccessat		307
#define __NR_pselect6		308
#define __NR_ppoll		309
#define __NR_unshare		310

/* Blackfin private syscalls */
#define __NR_sram_alloc		311
#define __NR_sram_free		312
#define __NR_dma_memcpy		313

/* socket syscalls */
#define __NR_accept		314
#define __NR_bind		315
#define __NR_connect		316
#define __NR_getpeername	317
#define __NR_getsockname	318
#define __NR_getsockopt		319
#define __NR_listen		320
#define __NR_recv		321
#define __NR_recvfrom		322
#define __NR_recvmsg		323
#define __NR_send		324
#define __NR_sendmsg		325
#define __NR_sendto		326
#define __NR_setsockopt		327
#define __NR_shutdown		328
#define __NR_socket		329
#define __NR_socketpair		330

/* sysv ipc syscalls */
#define __NR_semctl		331
#define __NR_semget		332
#define __NR_semop		333
#define __NR_msgctl		334
#define __NR_msgget		335
#define __NR_msgrcv		336
#define __NR_msgsnd		337
#define __NR_shmat		338
#define __NR_shmctl		339
#define __NR_shmdt		340
#define __NR_shmget		341

#define __NR_splice		342
#define __NR_sync_file_range	343
#define __NR_tee		344
#define __NR_vmsplice		345

#define __NR_epoll_pwait	346
#define __NR_utimensat		347
#define __NR_signalfd		348
#define __NR_timerfd_create	349
#define __NR_eventfd		350
#define __NR_pread64		351
#define __NR_pwrite64		352
#define __NR_fadvise64		353
#define __NR_set_robust_list	354
#define __NR_get_robust_list	355
#define __NR_fallocate		356
#define __NR_semtimedop		357
#define __NR_timerfd_settime	358
#define __NR_timerfd_gettime	359
#define __NR_signalfd4		360
#define __NR_eventfd2		361
#define __NR_epoll_create1	362
#define __NR_dup3		363
#define __NR_pipe2		364
#define __NR_inotify_init1	365
#define __NR_preadv		366
#define __NR_pwritev		367

#define __NR_syscall		368
#define NR_syscalls		__NR_syscall

/* Old optional stuff no one actually uses */
#define __IGNORE_sysfs
#define __IGNORE_uselib

/* Implement the newer interfaces */
#define __IGNORE_mmap
#define __IGNORE_poll
#define __IGNORE_select
#define __IGNORE_utime

/* Not relevant on no-mmu */
#define __IGNORE_swapon
#define __IGNORE_swapoff
#define __IGNORE_msync
#define __IGNORE_mlock
#define __IGNORE_munlock
#define __IGNORE_mlockall
#define __IGNORE_munlockall
#define __IGNORE_mincore
#define __IGNORE_madvise
#define __IGNORE_remap_file_pages
#define __IGNORE_mbind
#define __IGNORE_get_mempolicy
#define __IGNORE_set_mempolicy
#define __IGNORE_migrate_pages
#define __IGNORE_move_pages
#define __IGNORE_getcpu

#ifdef __KERNEL__
#define __ARCH_WANT_IPC_PARSE_VERSION
#define __ARCH_WANT_STAT64
#define __ARCH_WANT_SYS_ALARM
#define __ARCH_WANT_SYS_GETHOSTNAME
#define __ARCH_WANT_SYS_PAUSE
#define __ARCH_WANT_SYS_SGETMASK
#define __ARCH_WANT_SYS_TIME
#define __ARCH_WANT_SYS_FADVISE64
#define __ARCH_WANT_SYS_GETPGRP
#define __ARCH_WANT_SYS_LLSEEK
#define __ARCH_WANT_SYS_NICE
#define __ARCH_WANT_SYS_RT_SIGACTION
#define __ARCH_WANT_SYS_RT_SIGSUSPEND

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t_" #x "\n\t.set\t_" #x ",_sys_ni_syscall");

#endif	/* __KERNEL__ */

#endif				/* __ASM_BFIN_UNISTD_H */
