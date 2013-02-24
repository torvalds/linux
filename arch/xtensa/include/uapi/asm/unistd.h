#if !defined(_UAPI_XTENSA_UNISTD_H) || defined(__SYSCALL)
#define _UAPI_XTENSA_UNISTD_H

#ifndef __SYSCALL
# define __SYSCALL(nr,func,nargs)
#endif

#define __NR_spill				  0
__SYSCALL(  0, sys_ni_syscall, 0)
#define __NR_xtensa				  1
__SYSCALL(  1, sys_ni_syscall, 0)
#define __NR_available4				  2
__SYSCALL(  2, sys_ni_syscall, 0)
#define __NR_available5				  3
__SYSCALL(  3, sys_ni_syscall, 0)
#define __NR_available6				  4
__SYSCALL(  4, sys_ni_syscall, 0)
#define __NR_available7				  5
__SYSCALL(  5, sys_ni_syscall, 0)
#define __NR_available8				  6
__SYSCALL(  6, sys_ni_syscall, 0)
#define __NR_available9				  7
__SYSCALL(  7, sys_ni_syscall, 0)

/* File Operations */

#define __NR_open 				  8
__SYSCALL(  8, sys_open, 3)
#define __NR_close 				  9
__SYSCALL(  9, sys_close, 1)
#define __NR_dup 				 10
__SYSCALL( 10, sys_dup, 1)
#define __NR_dup2 				 11
__SYSCALL( 11, sys_dup2, 2)
#define __NR_read 				 12
__SYSCALL( 12, sys_read, 3)
#define __NR_write 				 13
__SYSCALL( 13, sys_write, 3)
#define __NR_select 				 14
__SYSCALL( 14, sys_select, 5)
#define __NR_lseek 				 15
__SYSCALL( 15, sys_lseek, 3)
#define __NR_poll 				 16
__SYSCALL( 16, sys_poll, 3)
#define __NR__llseek				 17
__SYSCALL( 17, sys_llseek, 5)
#define __NR_epoll_wait 			 18
__SYSCALL( 18, sys_epoll_wait, 4)
#define __NR_epoll_ctl 				 19
__SYSCALL( 19, sys_epoll_ctl, 4)
#define __NR_epoll_create 			 20
__SYSCALL( 20, sys_epoll_create, 1)
#define __NR_creat 				 21
__SYSCALL( 21, sys_creat, 2)
#define __NR_truncate 				 22
__SYSCALL( 22, sys_truncate, 2)
#define __NR_ftruncate 				 23
__SYSCALL( 23, sys_ftruncate, 2)
#define __NR_readv 				 24
__SYSCALL( 24, sys_readv, 3)
#define __NR_writev 				 25
__SYSCALL( 25, sys_writev, 3)
#define __NR_fsync 				 26
__SYSCALL( 26, sys_fsync, 1)
#define __NR_fdatasync 				 27
__SYSCALL( 27, sys_fdatasync, 1)
#define __NR_truncate64 			 28
__SYSCALL( 28, sys_truncate64, 2)
#define __NR_ftruncate64 			 29
__SYSCALL( 29, sys_ftruncate64, 2)
#define __NR_pread64 				 30
__SYSCALL( 30, sys_pread64, 6)
#define __NR_pwrite64 				 31
__SYSCALL( 31, sys_pwrite64, 6)

#define __NR_link 				 32
__SYSCALL( 32, sys_link, 2)
#define __NR_rename 				 33
__SYSCALL( 33, sys_rename, 2)
#define __NR_symlink 				 34
__SYSCALL( 34, sys_symlink, 2)
#define __NR_readlink 				 35
__SYSCALL( 35, sys_readlink, 3)
#define __NR_mknod 				 36
__SYSCALL( 36, sys_mknod, 3)
#define __NR_pipe 				 37
__SYSCALL( 37, sys_pipe, 1)
#define __NR_unlink 				 38
__SYSCALL( 38, sys_unlink, 1)
#define __NR_rmdir 				 39
__SYSCALL( 39, sys_rmdir, 1)

#define __NR_mkdir 				 40
__SYSCALL( 40, sys_mkdir, 2)
#define __NR_chdir 				 41
__SYSCALL( 41, sys_chdir, 1)
#define __NR_fchdir 				 42
__SYSCALL( 42, sys_fchdir, 1)
#define __NR_getcwd 				 43
__SYSCALL( 43, sys_getcwd, 2)

#define __NR_chmod 				 44
__SYSCALL( 44, sys_chmod, 2)
#define __NR_chown 				 45
__SYSCALL( 45, sys_chown, 3)
#define __NR_stat 				 46
__SYSCALL( 46, sys_newstat, 2)
#define __NR_stat64 				 47
__SYSCALL( 47, sys_stat64, 2)

#define __NR_lchown 				 48
__SYSCALL( 48, sys_lchown, 3)
#define __NR_lstat 				 49
__SYSCALL( 49, sys_newlstat, 2)
#define __NR_lstat64 				 50
__SYSCALL( 50, sys_lstat64, 2)
#define __NR_available51			 51
__SYSCALL( 51, sys_ni_syscall, 0)

#define __NR_fchmod 				 52
__SYSCALL( 52, sys_fchmod, 2)
#define __NR_fchown 				 53
__SYSCALL( 53, sys_fchown, 3)
#define __NR_fstat 				 54
__SYSCALL( 54, sys_newfstat, 2)
#define __NR_fstat64 				 55
__SYSCALL( 55, sys_fstat64, 2)

#define __NR_flock 				 56
__SYSCALL( 56, sys_flock, 2)
#define __NR_access 				 57
__SYSCALL( 57, sys_access, 2)
#define __NR_umask 				 58
__SYSCALL( 58, sys_umask, 1)
#define __NR_getdents 				 59
__SYSCALL( 59, sys_getdents, 3)
#define __NR_getdents64 			 60
__SYSCALL( 60, sys_getdents64, 3)
#define __NR_fcntl64 				 61
__SYSCALL( 61, sys_fcntl64, 3)
#define __NR_fallocate				 62
__SYSCALL( 62, sys_fallocate, 6)
#define __NR_fadvise64_64 			 63
__SYSCALL( 63, xtensa_fadvise64_64, 6)
#define __NR_utime				 64	/* glibc 2.3.3 ?? */
__SYSCALL( 64, sys_utime, 2)
#define __NR_utimes 				 65
__SYSCALL( 65, sys_utimes, 2)
#define __NR_ioctl 				 66
__SYSCALL( 66, sys_ioctl, 3)
#define __NR_fcntl 				 67
__SYSCALL( 67, sys_fcntl, 3)

#define __NR_setxattr 				 68
__SYSCALL( 68, sys_setxattr, 5)
#define __NR_getxattr 				 69
__SYSCALL( 69, sys_getxattr, 4)
#define __NR_listxattr 				 70
__SYSCALL( 70, sys_listxattr, 3)
#define __NR_removexattr 			 71
__SYSCALL( 71, sys_removexattr, 2)
#define __NR_lsetxattr 				 72
__SYSCALL( 72, sys_lsetxattr, 5)
#define __NR_lgetxattr 				 73
__SYSCALL( 73, sys_lgetxattr, 4)
#define __NR_llistxattr 			 74
__SYSCALL( 74, sys_llistxattr, 3)
#define __NR_lremovexattr 			 75
__SYSCALL( 75, sys_lremovexattr, 2)
#define __NR_fsetxattr 				 76
__SYSCALL( 76, sys_fsetxattr, 5)
#define __NR_fgetxattr 				 77
__SYSCALL( 77, sys_fgetxattr, 4)
#define __NR_flistxattr 			 78
__SYSCALL( 78, sys_flistxattr, 3)
#define __NR_fremovexattr 			 79
__SYSCALL( 79, sys_fremovexattr, 2)

/* File Map / Shared Memory Operations */

#define __NR_mmap2 				 80
__SYSCALL( 80, sys_mmap_pgoff, 6)
#define __NR_munmap 				 81
__SYSCALL( 81, sys_munmap, 2)
#define __NR_mprotect 				 82
__SYSCALL( 82, sys_mprotect, 3)
#define __NR_brk 				 83
__SYSCALL( 83, sys_brk, 1)
#define __NR_mlock 				 84
__SYSCALL( 84, sys_mlock, 2)
#define __NR_munlock 				 85
__SYSCALL( 85, sys_munlock, 2)
#define __NR_mlockall 				 86
__SYSCALL( 86, sys_mlockall, 1)
#define __NR_munlockall 			 87
__SYSCALL( 87, sys_munlockall, 0)
#define __NR_mremap 				 88
__SYSCALL( 88, sys_mremap, 4)
#define __NR_msync 				 89
__SYSCALL( 89, sys_msync, 3)
#define __NR_mincore 				 90
__SYSCALL( 90, sys_mincore, 3)
#define __NR_madvise 				 91
__SYSCALL( 91, sys_madvise, 3)
#define __NR_shmget				 92
__SYSCALL( 92, sys_shmget, 4)
#define __NR_shmat				 93
__SYSCALL( 93, xtensa_shmat, 4)
#define __NR_shmctl				 94
__SYSCALL( 94, sys_shmctl, 4)
#define __NR_shmdt				 95
__SYSCALL( 95, sys_shmdt, 4)

/* Socket Operations */

#define __NR_socket 				 96
__SYSCALL( 96, sys_socket, 3)
#define __NR_setsockopt 			 97
__SYSCALL( 97, sys_setsockopt, 5)
#define __NR_getsockopt 			 98
__SYSCALL( 98, sys_getsockopt, 5)
#define __NR_shutdown 				 99
__SYSCALL( 99, sys_shutdown, 2)

#define __NR_bind 				100
__SYSCALL(100, sys_bind, 3)
#define __NR_connect 				101
__SYSCALL(101, sys_connect, 3)
#define __NR_listen 				102
__SYSCALL(102, sys_listen, 2)
#define __NR_accept 				103
__SYSCALL(103, sys_accept, 3)

#define __NR_getsockname 			104
__SYSCALL(104, sys_getsockname, 3)
#define __NR_getpeername 			105
__SYSCALL(105, sys_getpeername, 3)
#define __NR_sendmsg 				106
__SYSCALL(106, sys_sendmsg, 3)
#define __NR_recvmsg 				107
__SYSCALL(107, sys_recvmsg, 3)
#define __NR_send 				108
__SYSCALL(108, sys_send, 4)
#define __NR_recv 				109
__SYSCALL(109, sys_recv, 4)
#define __NR_sendto 				110
__SYSCALL(110, sys_sendto, 6)
#define __NR_recvfrom 				111
__SYSCALL(111, sys_recvfrom, 6)

#define __NR_socketpair 			112
__SYSCALL(112, sys_socketpair, 4)
#define __NR_sendfile 				113
__SYSCALL(113, sys_sendfile, 4)
#define __NR_sendfile64 			114
__SYSCALL(114, sys_sendfile64, 4)
#define __NR_sendmmsg				115
__SYSCALL(115, sys_sendmmsg, 4)

/* Process Operations */

#define __NR_clone 				116
__SYSCALL(116, sys_clone, 5)
#define __NR_execve 				117
__SYSCALL(117, sys_execve, 3)
#define __NR_exit 				118
__SYSCALL(118, sys_exit, 1)
#define __NR_exit_group 			119
__SYSCALL(119, sys_exit_group, 1)
#define __NR_getpid 				120
__SYSCALL(120, sys_getpid, 0)
#define __NR_wait4 				121
__SYSCALL(121, sys_wait4, 4)
#define __NR_waitid 				122
__SYSCALL(122, sys_waitid, 5)
#define __NR_kill 				123
__SYSCALL(123, sys_kill, 2)
#define __NR_tkill 				124
__SYSCALL(124, sys_tkill, 2)
#define __NR_tgkill 				125
__SYSCALL(125, sys_tgkill, 3)
#define __NR_set_tid_address 			126
__SYSCALL(126, sys_set_tid_address, 1)
#define __NR_gettid 				127
__SYSCALL(127, sys_gettid, 0)
#define __NR_setsid 				128
__SYSCALL(128, sys_setsid, 0)
#define __NR_getsid 				129
__SYSCALL(129, sys_getsid, 1)
#define __NR_prctl 				130
__SYSCALL(130, sys_prctl, 5)
#define __NR_personality 			131
__SYSCALL(131, sys_personality, 1)
#define __NR_getpriority 			132
__SYSCALL(132, sys_getpriority, 2)
#define __NR_setpriority 			133
__SYSCALL(133, sys_setpriority, 3)
#define __NR_setitimer 				134
__SYSCALL(134, sys_setitimer, 3)
#define __NR_getitimer 				135
__SYSCALL(135, sys_getitimer, 2)
#define __NR_setuid 				136
__SYSCALL(136, sys_setuid, 1)
#define __NR_getuid 				137
__SYSCALL(137, sys_getuid, 0)
#define __NR_setgid 				138
__SYSCALL(138, sys_setgid, 1)
#define __NR_getgid 				139
__SYSCALL(139, sys_getgid, 0)
#define __NR_geteuid 				140
__SYSCALL(140, sys_geteuid, 0)
#define __NR_getegid 				141
__SYSCALL(141, sys_getegid, 0)
#define __NR_setreuid 				142
__SYSCALL(142, sys_setreuid, 2)
#define __NR_setregid 				143
__SYSCALL(143, sys_setregid, 2)
#define __NR_setresuid 				144
__SYSCALL(144, sys_setresuid, 3)
#define __NR_getresuid 				145
__SYSCALL(145, sys_getresuid, 3)
#define __NR_setresgid 				146
__SYSCALL(146, sys_setresgid, 3)
#define __NR_getresgid 				147
__SYSCALL(147, sys_getresgid, 3)
#define __NR_setpgid 				148
__SYSCALL(148, sys_setpgid, 2)
#define __NR_getpgid 				149
__SYSCALL(149, sys_getpgid, 1)
#define __NR_getppid 				150
__SYSCALL(150, sys_getppid, 0)
#define __NR_getpgrp				151
__SYSCALL(151, sys_getpgrp, 0)

#define __NR_reserved152 			152	/* set_thread_area */
__SYSCALL(152, sys_ni_syscall, 0)
#define __NR_reserved153 			153	/* get_thread_area */
__SYSCALL(153, sys_ni_syscall, 0)
#define __NR_times 				154
__SYSCALL(154, sys_times, 1)
#define __NR_acct 				155
__SYSCALL(155, sys_acct, 1)
#define __NR_sched_setaffinity 			156
__SYSCALL(156, sys_sched_setaffinity, 3)
#define __NR_sched_getaffinity 			157
__SYSCALL(157, sys_sched_getaffinity, 3)
#define __NR_capget 				158
__SYSCALL(158, sys_capget, 2)
#define __NR_capset 				159
__SYSCALL(159, sys_capset, 2)
#define __NR_ptrace 				160
__SYSCALL(160, sys_ptrace, 4)
#define __NR_semtimedop				161
__SYSCALL(161, sys_semtimedop, 5)
#define __NR_semget				162
__SYSCALL(162, sys_semget, 4)
#define __NR_semop				163
__SYSCALL(163, sys_semop, 4)
#define __NR_semctl				164
__SYSCALL(164, sys_semctl, 4)
#define __NR_available165			165
__SYSCALL(165, sys_ni_syscall, 0)
#define __NR_msgget				166
__SYSCALL(166, sys_msgget, 4)
#define __NR_msgsnd				167
__SYSCALL(167, sys_msgsnd, 4)
#define __NR_msgrcv				168
__SYSCALL(168, sys_msgrcv, 4)
#define __NR_msgctl				169
__SYSCALL(169, sys_msgctl, 4)
#define __NR_available170			170
__SYSCALL(170, sys_ni_syscall, 0)

/* File System */

#define __NR_umount2				171
__SYSCALL(171, sys_umount, 2)
#define __NR_mount 				172
__SYSCALL(172, sys_mount, 5)
#define __NR_swapon 				173
__SYSCALL(173, sys_swapon, 2)
#define __NR_chroot 				174
__SYSCALL(174, sys_chroot, 1)
#define __NR_pivot_root 			175
__SYSCALL(175, sys_pivot_root, 2)
#define __NR_umount 				176
__SYSCALL(176, sys_umount, 2)
#define __NR_swapoff 				177
__SYSCALL(177, sys_swapoff, 1)
#define __NR_sync 				178
__SYSCALL(178, sys_sync, 0)
#define __NR_syncfs				179
__SYSCALL(179, sys_syncfs, 1)
#define __NR_setfsuid 				180
__SYSCALL(180, sys_setfsuid, 1)
#define __NR_setfsgid 				181
__SYSCALL(181, sys_setfsgid, 1)
#define __NR_sysfs 				182
__SYSCALL(182, sys_sysfs, 3)
#define __NR_ustat 				183
__SYSCALL(183, sys_ustat, 2)
#define __NR_statfs 				184
__SYSCALL(184, sys_statfs, 2)
#define __NR_fstatfs 				185
__SYSCALL(185, sys_fstatfs, 2)
#define __NR_statfs64 				186
__SYSCALL(186, sys_statfs64, 3)
#define __NR_fstatfs64 				187
__SYSCALL(187, sys_fstatfs64, 3)

/* System */

#define __NR_setrlimit 				188
__SYSCALL(188, sys_setrlimit, 2)
#define __NR_getrlimit 				189
__SYSCALL(189, sys_getrlimit, 2)
#define __NR_getrusage 				190
__SYSCALL(190, sys_getrusage, 2)
#define __NR_futex				191
__SYSCALL(191, sys_futex, 5)
#define __NR_gettimeofday 			192
__SYSCALL(192, sys_gettimeofday, 2)
#define __NR_settimeofday 			193
__SYSCALL(193, sys_settimeofday, 2)
#define __NR_adjtimex 				194
__SYSCALL(194, sys_adjtimex, 1)
#define __NR_nanosleep	 			195
__SYSCALL(195, sys_nanosleep, 2)
#define __NR_getgroups 				196
__SYSCALL(196, sys_getgroups, 2)
#define __NR_setgroups 				197
__SYSCALL(197, sys_setgroups, 2)
#define __NR_sethostname 			198
__SYSCALL(198, sys_sethostname, 2)
#define __NR_setdomainname 			199
__SYSCALL(199, sys_setdomainname, 2)
#define __NR_syslog 				200
__SYSCALL(200, sys_syslog, 3)
#define __NR_vhangup 				201
__SYSCALL(201, sys_vhangup, 0)
#define __NR_uselib 				202
__SYSCALL(202, sys_uselib, 1)
#define __NR_reboot 				203
__SYSCALL(203, sys_reboot, 3)
#define __NR_quotactl 				204
__SYSCALL(204, sys_quotactl, 4)
#define __NR_nfsservctl 			205
__SYSCALL(205, sys_ni_syscall, 0)			/* old nfsservctl */
#define __NR__sysctl 				206
__SYSCALL(206, sys_sysctl, 1)
#define __NR_bdflush 				207
__SYSCALL(207, sys_bdflush, 2)
#define __NR_uname 				208
__SYSCALL(208, sys_newuname, 1)
#define __NR_sysinfo 				209
__SYSCALL(209, sys_sysinfo, 1)
#define __NR_init_module 			210
__SYSCALL(210, sys_init_module, 2)
#define __NR_delete_module 			211
__SYSCALL(211, sys_delete_module, 1)

#define __NR_sched_setparam 			212
__SYSCALL(212, sys_sched_setparam, 2)
#define __NR_sched_getparam 			213
__SYSCALL(213, sys_sched_getparam, 2)
#define __NR_sched_setscheduler 		214
__SYSCALL(214, sys_sched_setscheduler, 3)
#define __NR_sched_getscheduler 		215
__SYSCALL(215, sys_sched_getscheduler, 1)
#define __NR_sched_get_priority_max 		216
__SYSCALL(216, sys_sched_get_priority_max, 1)
#define __NR_sched_get_priority_min 		217
__SYSCALL(217, sys_sched_get_priority_min, 1)
#define __NR_sched_rr_get_interval 		218
__SYSCALL(218, sys_sched_rr_get_interval, 2)
#define __NR_sched_yield 			219
__SYSCALL(219, sys_sched_yield, 0)
#define __NR_available222 			222
__SYSCALL(222, sys_ni_syscall, 0)

/* Signal Handling */

#define __NR_restart_syscall 			223
__SYSCALL(223, sys_restart_syscall, 0)
#define __NR_sigaltstack 			224
__SYSCALL(224, sys_sigaltstack, 2)
#define __NR_rt_sigreturn 			225
__SYSCALL(225, xtensa_rt_sigreturn, 1)
#define __NR_rt_sigaction 			226
__SYSCALL(226, sys_rt_sigaction, 4)
#define __NR_rt_sigprocmask 			227
__SYSCALL(227, sys_rt_sigprocmask, 4)
#define __NR_rt_sigpending 			228
__SYSCALL(228, sys_rt_sigpending, 2)
#define __NR_rt_sigtimedwait 			229
__SYSCALL(229, sys_rt_sigtimedwait, 4)
#define __NR_rt_sigqueueinfo 			230
__SYSCALL(230, sys_rt_sigqueueinfo, 3)
#define __NR_rt_sigsuspend 			231
__SYSCALL(231, sys_rt_sigsuspend, 2)

/* Message */

#define __NR_mq_open 				232
__SYSCALL(232, sys_mq_open, 4)
#define __NR_mq_unlink 				233
__SYSCALL(233, sys_mq_unlink, 1)
#define __NR_mq_timedsend 			234
__SYSCALL(234, sys_mq_timedsend, 5)
#define __NR_mq_timedreceive 			235
__SYSCALL(235, sys_mq_timedreceive, 5)
#define __NR_mq_notify 				236
__SYSCALL(236, sys_mq_notify, 2)
#define __NR_mq_getsetattr 			237
__SYSCALL(237, sys_mq_getsetattr, 3)
#define __NR_available238			238
__SYSCALL(238, sys_ni_syscall, 0)

/* IO */

#define __NR_io_setup 				239
__SYSCALL(239, sys_io_setup, 2)
#define __NR_io_destroy 			240
__SYSCALL(240, sys_io_destroy, 1)
#define __NR_io_submit 				241
__SYSCALL(241, sys_io_submit, 3)
#define __NR_io_getevents 			242
__SYSCALL(242, sys_io_getevents, 5)
#define __NR_io_cancel 				243
__SYSCALL(243, sys_io_cancel, 3)
#define __NR_clock_settime 			244
__SYSCALL(244, sys_clock_settime, 2)
#define __NR_clock_gettime 			245
__SYSCALL(245, sys_clock_gettime, 2)
#define __NR_clock_getres 			246
__SYSCALL(246, sys_clock_getres, 2)
#define __NR_clock_nanosleep 			247
__SYSCALL(247, sys_clock_nanosleep, 4)

/* Timer */

#define __NR_timer_create 			248
__SYSCALL(248, sys_timer_create, 3)
#define __NR_timer_delete 			249
__SYSCALL(249, sys_timer_delete, 1)
#define __NR_timer_settime 			250
__SYSCALL(250, sys_timer_settime, 4)
#define __NR_timer_gettime 			251
__SYSCALL(251, sys_timer_gettime, 2)
#define __NR_timer_getoverrun 			252
__SYSCALL(252, sys_timer_getoverrun, 1)

/* System */

#define __NR_reserved253			253
__SYSCALL(253, sys_ni_syscall, 0)
#define __NR_lookup_dcookie 			254
__SYSCALL(254, sys_lookup_dcookie, 4)
#define __NR_available255			255
__SYSCALL(255, sys_ni_syscall, 0)
#define __NR_add_key 				256
__SYSCALL(256, sys_add_key, 5)
#define __NR_request_key 			257
__SYSCALL(257, sys_request_key, 5)
#define __NR_keyctl 				258
__SYSCALL(258, sys_keyctl, 5)
#define __NR_available259			259
__SYSCALL(259, sys_ni_syscall, 0)


#define __NR_readahead				260
__SYSCALL(260, sys_readahead, 5)
#define __NR_remap_file_pages			261
__SYSCALL(261, sys_remap_file_pages, 5)
#define __NR_migrate_pages			262
__SYSCALL(262, sys_migrate_pages, 0)
#define __NR_mbind				263
__SYSCALL(263, sys_mbind, 6)
#define __NR_get_mempolicy			264
__SYSCALL(264, sys_get_mempolicy, 5)
#define __NR_set_mempolicy			265
__SYSCALL(265, sys_set_mempolicy, 3)
#define __NR_unshare				266
__SYSCALL(266, sys_unshare, 1)
#define __NR_move_pages				267
__SYSCALL(267, sys_move_pages, 0)
#define __NR_splice				268
__SYSCALL(268, sys_splice, 0)
#define __NR_tee				269
__SYSCALL(269, sys_tee, 0)
#define __NR_vmsplice				270
__SYSCALL(270, sys_vmsplice, 0)
#define __NR_available271			271
__SYSCALL(271, sys_ni_syscall, 0)

#define __NR_pselect6				272
__SYSCALL(272, sys_pselect6, 0)
#define __NR_ppoll				273
__SYSCALL(273, sys_ppoll, 0)
#define __NR_epoll_pwait			274
__SYSCALL(274, sys_epoll_pwait, 0)
#define __NR_epoll_create1		275
__SYSCALL(275, sys_epoll_create1, 1)

#define __NR_inotify_init			276
__SYSCALL(276, sys_inotify_init, 0)
#define __NR_inotify_add_watch			277
__SYSCALL(277, sys_inotify_add_watch, 3)
#define __NR_inotify_rm_watch			278
__SYSCALL(278, sys_inotify_rm_watch, 2)
#define __NR_inotify_init1			279
__SYSCALL(279, sys_inotify_init1, 1)

#define __NR_getcpu				280
__SYSCALL(280, sys_getcpu, 0)
#define __NR_kexec_load				281
__SYSCALL(281, sys_ni_syscall, 0)

#define __NR_ioprio_set				282
__SYSCALL(282, sys_ioprio_set, 2)
#define __NR_ioprio_get				283
__SYSCALL(283, sys_ioprio_get, 3)

#define __NR_set_robust_list			284
__SYSCALL(284, sys_set_robust_list, 3)
#define __NR_get_robust_list			285
__SYSCALL(285, sys_get_robust_list, 3)
#define __NR_available286			286
__SYSCALL(286, sys_ni_syscall, 0)
#define __NR_available287			287
__SYSCALL(287, sys_ni_syscall, 0)

/* Relative File Operations */

#define __NR_openat				288
__SYSCALL(288, sys_openat, 4)
#define __NR_mkdirat				289
__SYSCALL(289, sys_mkdirat, 3)
#define __NR_mknodat				290
__SYSCALL(290, sys_mknodat, 4)
#define __NR_unlinkat				291
__SYSCALL(291, sys_unlinkat, 3)
#define __NR_renameat				292
__SYSCALL(292, sys_renameat, 4)
#define __NR_linkat				293
__SYSCALL(293, sys_linkat, 5)
#define __NR_symlinkat				294
__SYSCALL(294, sys_symlinkat, 3)
#define __NR_readlinkat				295
__SYSCALL(295, sys_readlinkat, 4)
#define __NR_utimensat				296
__SYSCALL(296, sys_utimensat, 0)
#define __NR_fchownat				297
__SYSCALL(297, sys_fchownat, 5)
#define __NR_futimesat				298
__SYSCALL(298, sys_futimesat, 4)
#define __NR_fstatat64				299
__SYSCALL(299, sys_fstatat64, 0)
#define __NR_fchmodat				300
__SYSCALL(300, sys_fchmodat, 4)
#define __NR_faccessat				301
__SYSCALL(301, sys_faccessat, 4)
#define __NR_available302			302
__SYSCALL(302, sys_ni_syscall, 0)
#define __NR_available303			303
__SYSCALL(303, sys_ni_syscall, 0)

#define __NR_signalfd				304
__SYSCALL(304, sys_signalfd, 3)
/*  305 was __NR_timerfd  */
__SYSCALL(305, sys_ni_syscall, 0)
#define __NR_eventfd				306
__SYSCALL(306, sys_eventfd, 1)
#define __NR_recvmmsg				307
__SYSCALL(307, sys_recvmmsg, 5)

#define __NR_setns				308
__SYSCALL(308, sys_setns, 2)
#define __NR_signalfd4				309
__SYSCALL(309, sys_signalfd4, 4)
#define __NR_dup3				310
__SYSCALL(310, sys_dup3, 3)
#define __NR_pipe2				311
__SYSCALL(311, sys_pipe2, 2)

#define __NR_timerfd_create			312
__SYSCALL(312, sys_timerfd_create, 2)
#define __NR_timerfd_settime			313
__SYSCALL(313, sys_timerfd_settime, 4)
#define __NR_timerfd_gettime			314
__SYSCALL(314, sys_timerfd_gettime, 2)
#define __NR_available315			315
__SYSCALL(315, sys_ni_syscall, 0)

#define __NR_eventfd2				316
__SYSCALL(316, sys_eventfd2, 2)
#define __NR_preadv				317
__SYSCALL(317, sys_preadv, 5)
#define __NR_pwritev				318
__SYSCALL(318, sys_pwritev, 5)
#define __NR_available319			319
__SYSCALL(319, sys_ni_syscall, 0)

#define __NR_fanotify_init			320
__SYSCALL(320, sys_fanotify_init, 2)
#define __NR_fanotify_mark			321
__SYSCALL(321, sys_fanotify_mark, 6)
#define __NR_process_vm_readv			322
__SYSCALL(322, sys_process_vm_readv, 6)
#define __NR_process_vm_writev			323
__SYSCALL(323, sys_process_vm_writev, 6)

#define __NR_name_to_handle_at			324
__SYSCALL(324, sys_name_to_handle_at, 5)
#define __NR_open_by_handle_at			325
__SYSCALL(325, sys_open_by_handle_at, 3)
#define __NR_sync_file_range			326
__SYSCALL(326, sys_sync_file_range2, 6)
#define __NR_perf_event_open			327
__SYSCALL(327, sys_perf_event_open, 5)

#define __NR_rt_tgsigqueueinfo			328
__SYSCALL(328, sys_rt_tgsigqueueinfo, 4)
#define __NR_clock_adjtime			329
__SYSCALL(329, sys_clock_adjtime, 2)
#define __NR_prlimit64				330
__SYSCALL(330, sys_prlimit64, 4)
#define __NR_kcmp				331
__SYSCALL(331, sys_kcmp, 5)


#define __NR_syscall_count			332

/*
 * sysxtensa syscall handler
 *
 * int sysxtensa (SYS_XTENSA_ATOMIC_SET,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_ADD,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_EXG_ADD, ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_CMP_SWP, ptr, oldval, newval);
 *        a2            a6                   a3    a4      a5
 */

#define SYS_XTENSA_RESERVED               0     /* don't use this */
#define SYS_XTENSA_ATOMIC_SET             1     /* set variable */
#define SYS_XTENSA_ATOMIC_EXG_ADD         2     /* exchange memory and add */
#define SYS_XTENSA_ATOMIC_ADD             3     /* add to memory */
#define SYS_XTENSA_ATOMIC_CMP_SWP         4     /* compare and swap */

#define SYS_XTENSA_COUNT                  5     /* count */

#undef __SYSCALL

#endif /* _UAPI_XTENSA_UNISTD_H */
