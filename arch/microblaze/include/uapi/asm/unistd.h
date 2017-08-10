/*
 * Copyright (C) 2007-2008 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _UAPI_ASM_MICROBLAZE_UNISTD_H
#define _UAPI_ASM_MICROBLAZE_UNISTD_H

#define __NR_restart_syscall	0 /* ok */
#define __NR_exit		1 /* ok */
#define __NR_fork		2 /* not for no MMU - weird */
#define __NR_read		3 /* ok */
#define __NR_write		4 /* ok */
#define __NR_open		5 /* openat */
#define __NR_close		6 /* ok */
#define __NR_waitpid		7 /* waitid */
#define __NR_creat		8 /* openat */
#define __NR_link		9 /* linkat */
#define __NR_unlink		10 /* unlinkat */
#define __NR_execve		11 /* ok */
#define __NR_chdir		12 /* ok */
#define __NR_time		13 /* obsolete -> sys_gettimeofday */
#define __NR_mknod		14 /* mknodat */
#define __NR_chmod		15 /* fchmodat */
#define __NR_lchown		16 /* ok */
#define __NR_break		17 /* don't know */
#define __NR_oldstat		18 /* remove */
#define __NR_lseek		19 /* ok */
#define __NR_getpid		20 /* ok */
#define __NR_mount		21 /* ok */
#define __NR_umount		22 /* ok */  /* use only umount2 */
#define __NR_setuid		23 /* ok */
#define __NR_getuid		24 /* ok */
#define __NR_stime		25 /* obsolete -> sys_settimeofday */
#define __NR_ptrace		26 /* ok */
#define __NR_alarm		27 /* obsolete -> sys_setitimer */
#define __NR_oldfstat		28 /* remove */
#define __NR_pause		29 /* obsolete -> sys_rt_sigtimedwait */
#define __NR_utime		30 /* obsolete -> sys_utimesat */
#define __NR_stty		31 /* remove */
#define __NR_gtty		32 /* remove */
#define __NR_access		33 /* faccessat */
/* can be implemented by sys_setpriority */
#define __NR_nice		34
#define __NR_ftime		35 /* remove */
#define __NR_sync		36 /* ok */
#define __NR_kill		37 /* ok */
#define __NR_rename		38 /* renameat */
#define __NR_mkdir		39 /* mkdirat */
#define __NR_rmdir		40 /* unlinkat */
#define __NR_dup		41 /* ok */
#define __NR_pipe		42 /* ok */
#define __NR_times		43 /* ok */
#define __NR_prof		44 /* remove */
#define __NR_brk		45 /* ok -mmu, nommu specific */
#define __NR_setgid		46 /* ok */
#define __NR_getgid		47 /* ok */
#define __NR_signal		48 /* obsolete -> sys_rt_sigaction */
#define __NR_geteuid		49 /* ok */
#define __NR_getegid		50 /* ok */
#define __NR_acct		51 /* add it and then I can disable it */
#define __NR_umount2		52 /* remove */
#define __NR_lock		53 /* remove */
#define __NR_ioctl		54 /* ok */
#define __NR_fcntl		55 /* ok -> 64bit version*/
#define __NR_mpx		56 /* remove */
#define __NR_setpgid		57 /* ok */
#define __NR_ulimit		58 /* remove */
#define __NR_oldolduname	59 /* remove */
#define __NR_umask		60 /* ok */
#define __NR_chroot		61 /* ok */
#define __NR_ustat		62 /* obsolete -> statfs64 */
#define __NR_dup2		63 /* ok */
#define __NR_getppid		64 /* ok */
#define __NR_getpgrp		65 /* obsolete -> sys_getpgid */
#define __NR_setsid		66 /* ok */
#define __NR_sigaction		67 /* obsolete -> rt_sigaction */
#define __NR_sgetmask		68 /* obsolete -> sys_rt_sigprocmask */
#define __NR_ssetmask		69 /* obsolete ->sys_rt_sigprocmask */
#define __NR_setreuid		70 /* ok */
#define __NR_setregid		71 /* ok */
#define __NR_sigsuspend		72 /* obsolete -> rt_sigsuspend */
#define __NR_sigpending		73 /* obsolete -> sys_rt_sigpending */
#define __NR_sethostname	74 /* ok */
#define __NR_setrlimit		75 /* ok */
#define __NR_getrlimit		76 /* ok Back compatible 2G limited rlimit */
#define __NR_getrusage		77 /* ok */
#define __NR_gettimeofday	78 /* ok */
#define __NR_settimeofday	79 /* ok */
#define __NR_getgroups		80 /* ok */
#define __NR_setgroups		81 /* ok */
#define __NR_select		82 /* obsolete -> sys_pselect6 */
#define __NR_symlink		83 /* symlinkat */
#define __NR_oldlstat		84 /* remove */
#define __NR_readlink		85 /* obsolete -> sys_readlinkat */
#define __NR_uselib		86 /* remove */
#define __NR_swapon		87 /* ok */
#define __NR_reboot		88 /* ok */
#define __NR_readdir		89 /* remove ? */
#define __NR_mmap		90 /* obsolete -> sys_mmap2 */
#define __NR_munmap		91 /* ok - mmu and nommu */
#define __NR_truncate		92 /* ok or truncate64 */
#define __NR_ftruncate		93 /* ok or ftruncate64 */
#define __NR_fchmod		94 /* ok */
#define __NR_fchown		95 /* ok */
#define __NR_getpriority	96 /* ok */
#define __NR_setpriority	97 /* ok */
#define __NR_profil		98 /* remove */
#define __NR_statfs		99 /* ok or statfs64 */
#define __NR_fstatfs		100  /* ok or fstatfs64 */
#define __NR_ioperm		101 /* remove */
#define __NR_socketcall		102 /* remove */
#define __NR_syslog		103 /* ok */
#define __NR_setitimer		104 /* ok */
#define __NR_getitimer		105 /* ok */
#define __NR_stat		106 /* remove */
#define __NR_lstat		107 /* remove */
#define __NR_fstat		108 /* remove */
#define __NR_olduname		109 /* remove */
#define __NR_iopl		110 /* remove */
#define __NR_vhangup		111 /* ok */
#define __NR_idle		112 /* remove */
#define __NR_vm86old		113 /* remove */
#define __NR_wait4		114 /* obsolete -> waitid */
#define __NR_swapoff		115 /* ok */
#define __NR_sysinfo		116 /* ok */
#define __NR_ipc		117 /* remove - direct call */
#define __NR_fsync		118 /* ok */
#define __NR_sigreturn		119 /* obsolete -> sys_rt_sigreturn */
#define __NR_clone		120 /* ok */
#define __NR_setdomainname	121 /* ok */
#define __NR_uname		122 /* remove */
#define __NR_modify_ldt		123 /* remove */
#define __NR_adjtimex		124 /* ok */
#define __NR_mprotect		125 /* remove */
#define __NR_sigprocmask	126 /* obsolete -> sys_rt_sigprocmask */
#define __NR_create_module	127 /* remove */
#define __NR_init_module	128 /* ok */
#define __NR_delete_module	129 /* ok */
#define __NR_get_kernel_syms	130 /* remove */
#define __NR_quotactl		131 /* ok */
#define __NR_getpgid		132 /* ok */
#define __NR_fchdir		133 /* ok */
#define __NR_bdflush		134 /* remove */
#define __NR_sysfs		135 /* needed for busybox */
#define __NR_personality	136 /* ok */
#define __NR_afs_syscall	137 /* Syscall for Andrew File System */
#define __NR_setfsuid		138 /* ok */
#define __NR_setfsgid		139 /* ok */
#define __NR__llseek		140 /* remove only lseek */
#define __NR_getdents		141 /* ok or getdents64 */
#define __NR__newselect		142 /* remove */
#define __NR_flock		143 /* ok */
#define __NR_msync		144 /* remove */
#define __NR_readv		145 /* ok */
#define __NR_writev		146 /* ok */
#define __NR_getsid		147 /* ok */
#define __NR_fdatasync		148 /* ok */
#define __NR__sysctl		149 /* remove */
#define __NR_mlock		150 /* ok - nommu or mmu */
#define __NR_munlock		151 /* ok - nommu or mmu */
#define __NR_mlockall		152 /* ok - nommu or mmu */
#define __NR_munlockall		153 /* ok - nommu or mmu */
#define __NR_sched_setparam		154 /* ok */
#define __NR_sched_getparam		155 /* ok */
#define __NR_sched_setscheduler		156 /* ok */
#define __NR_sched_getscheduler		157 /* ok */
#define __NR_sched_yield		158 /* ok */
#define __NR_sched_get_priority_max	159 /* ok */
#define __NR_sched_get_priority_min	160 /* ok */
#define __NR_sched_rr_get_interval	161 /* ok */
#define __NR_nanosleep		162 /* ok */
#define __NR_mremap		163 /* ok - nommu or mmu */
#define __NR_setresuid		164 /* ok */
#define __NR_getresuid		165 /* ok */
#define __NR_vm86		166 /* remove */
#define __NR_query_module	167 /* ok */
#define __NR_poll		168 /* obsolete -> sys_ppoll */
#define __NR_nfsservctl		169 /* ok */
#define __NR_setresgid		170 /* ok */
#define __NR_getresgid		171 /* ok */
#define __NR_prctl		172 /* ok */
#define __NR_rt_sigreturn	173 /* ok */
#define __NR_rt_sigaction	174 /* ok */
#define __NR_rt_sigprocmask	175 /* ok */
#define __NR_rt_sigpending	176 /* ok */
#define __NR_rt_sigtimedwait	177 /* ok */
#define __NR_rt_sigqueueinfo	178 /* ok */
#define __NR_rt_sigsuspend	179 /* ok */
#define __NR_pread64		180 /* ok */
#define __NR_pwrite64		181 /* ok */
#define __NR_chown		182 /* obsolete -> fchownat */
#define __NR_getcwd		183 /* ok */
#define __NR_capget		184 /* ok */
#define __NR_capset		185 /* ok */
#define __NR_sigaltstack	186 /* remove */
#define __NR_sendfile		187 /* ok -> exist 64bit version*/
#define __NR_getpmsg		188 /* remove */
/* remove - some people actually want streams */
#define __NR_putpmsg		189
/* for noMMU - group with clone -> maybe remove */
#define __NR_vfork		190
#define __NR_ugetrlimit		191 /* remove - SuS compliant getrlimit */
#define __NR_mmap2		192 /* ok */
#define __NR_truncate64		193 /* ok */
#define __NR_ftruncate64	194 /* ok */
#define __NR_stat64		195 /* remove _ARCH_WANT_STAT64 */
#define __NR_lstat64		196 /* remove _ARCH_WANT_STAT64 */
#define __NR_fstat64		197 /* remove _ARCH_WANT_STAT64 */
#define __NR_lchown32		198 /* ok - without 32 */
#define __NR_getuid32		199 /* ok - without 32 */
#define __NR_getgid32		200 /* ok - without 32 */
#define __NR_geteuid32		201 /* ok - without 32 */
#define __NR_getegid32		202 /* ok - without 32 */
#define __NR_setreuid32		203 /* ok - without 32 */
#define __NR_setregid32		204 /* ok - without 32 */
#define __NR_getgroups32	205 /* ok - without 32 */
#define __NR_setgroups32	206 /* ok - without 32 */
#define __NR_fchown32		207 /* ok - without 32 */
#define __NR_setresuid32	208 /* ok - without 32 */
#define __NR_getresuid32	209 /* ok - without 32 */
#define __NR_setresgid32	210 /* ok - without 32 */
#define __NR_getresgid32	211 /* ok - without 32 */
#define __NR_chown32		212 /* ok - without 32 -obsolete -> fchownat */
#define __NR_setuid32		213 /* ok - without 32 */
#define __NR_setgid32		214 /* ok - without 32 */
#define __NR_setfsuid32		215 /* ok - without 32 */
#define __NR_setfsgid32		216 /* ok - without 32 */
#define __NR_pivot_root		217 /* ok */
#define __NR_mincore		218 /* ok */
#define __NR_madvise		219 /* ok */
#define __NR_getdents64		220 /* ok */
#define __NR_fcntl64		221 /* ok */
/* 223 is unused */
#define __NR_gettid		224 /* ok */
#define __NR_readahead		225 /* ok */
#define __NR_setxattr		226 /* ok */
#define __NR_lsetxattr		227 /* ok */
#define __NR_fsetxattr		228 /* ok */
#define __NR_getxattr		229 /* ok */
#define __NR_lgetxattr		230 /* ok */
#define __NR_fgetxattr		231 /* ok */
#define __NR_listxattr		232 /* ok */
#define __NR_llistxattr		233 /* ok */
#define __NR_flistxattr		234 /* ok */
#define __NR_removexattr	235 /* ok */
#define __NR_lremovexattr	236 /* ok */
#define __NR_fremovexattr	237 /* ok */
#define __NR_tkill		238 /* ok */
#define __NR_sendfile64		239 /* ok */
#define __NR_futex		240 /* ok */
#define __NR_sched_setaffinity	241 /* ok */
#define __NR_sched_getaffinity	242 /* ok */
#define __NR_set_thread_area	243 /* remove */
#define __NR_get_thread_area	244 /* remove */
#define __NR_io_setup		245 /* ok */
#define __NR_io_destroy		246 /* ok */
#define __NR_io_getevents	247 /* ok */
#define __NR_io_submit		248 /* ok */
#define __NR_io_cancel		249 /* ok */
#define __NR_fadvise64		250 /* remove -> sys_fadvise64_64 */
/* 251 is available for reuse (was briefly sys_set_zone_reclaim) */
#define __NR_exit_group		252 /* ok */
#define __NR_lookup_dcookie	253 /* ok */
#define __NR_epoll_create	254 /* ok */
#define __NR_epoll_ctl		255 /* ok */
#define __NR_epoll_wait		256 /* obsolete -> sys_epoll_pwait */
#define __NR_remap_file_pages	257 /* only for mmu */
#define __NR_set_tid_address	258 /* ok */
#define __NR_timer_create	259 /* ok */
#define __NR_timer_settime	(__NR_timer_create+1) /* 260 */ /* ok */
#define __NR_timer_gettime	(__NR_timer_create+2) /* 261 */ /* ok */
#define __NR_timer_getoverrun	(__NR_timer_create+3) /* 262 */ /* ok */
#define __NR_timer_delete	(__NR_timer_create+4) /* 263 */ /* ok */
#define __NR_clock_settime	(__NR_timer_create+5) /* 264 */ /* ok */
#define __NR_clock_gettime	(__NR_timer_create+6) /* 265 */ /* ok */
#define __NR_clock_getres	(__NR_timer_create+7) /* 266 */ /* ok */
#define __NR_clock_nanosleep	(__NR_timer_create+8) /* 267 */ /* ok */
#define __NR_statfs64		268 /* ok */
#define __NR_fstatfs64		269 /* ok */
#define __NR_tgkill		270 /* ok */
#define __NR_utimes		271 /* obsolete -> sys_futimesat */
#define __NR_fadvise64_64	272 /* ok */
#define __NR_vserver		273 /* ok */
#define __NR_mbind		274 /* only for mmu */
#define __NR_get_mempolicy	275 /* only for mmu */
#define __NR_set_mempolicy	276 /* only for mmu */
#define __NR_mq_open		277 /* ok */
#define __NR_mq_unlink		(__NR_mq_open+1) /* 278 */ /* ok */
#define __NR_mq_timedsend	(__NR_mq_open+2) /* 279 */ /* ok */
#define __NR_mq_timedreceive	(__NR_mq_open+3) /* 280 */ /* ok */
#define __NR_mq_notify		(__NR_mq_open+4) /* 281 */ /* ok */
#define __NR_mq_getsetattr	(__NR_mq_open+5) /* 282 */ /* ok */
#define __NR_kexec_load		283 /* ok */
#define __NR_waitid		284 /* ok */
/* #define __NR_sys_setaltroot	285 */
#define __NR_add_key		286 /* ok */
#define __NR_request_key	287 /* ok */
#define __NR_keyctl		288 /* ok */
#define __NR_ioprio_set		289 /* ok */
#define __NR_ioprio_get		290 /* ok */
#define __NR_inotify_init	291 /* ok */
#define __NR_inotify_add_watch	292 /* ok */
#define __NR_inotify_rm_watch	293 /* ok */
#define __NR_migrate_pages	294 /* mmu */
#define __NR_openat		295 /* ok */
#define __NR_mkdirat		296 /* ok */
#define __NR_mknodat		297 /* ok */
#define __NR_fchownat		298 /* ok */
#define __NR_futimesat		299 /* obsolete -> sys_utimesat */
#define __NR_fstatat64		300 /* stat64 */
#define __NR_unlinkat		301 /* ok */
#define __NR_renameat		302 /* ok */
#define __NR_linkat		303 /* ok */
#define __NR_symlinkat		304 /* ok */
#define __NR_readlinkat		305 /* ok */
#define __NR_fchmodat		306 /* ok */
#define __NR_faccessat		307 /* ok */
#define __NR_pselect6		308 /* ok */
#define __NR_ppoll		309 /* ok */
#define __NR_unshare		310 /* ok */
#define __NR_set_robust_list	311 /* ok */
#define __NR_get_robust_list	312 /* ok */
#define __NR_splice		313 /* ok */
#define __NR_sync_file_range	314 /* ok */
#define __NR_tee		315 /* ok */
#define __NR_vmsplice		316 /* ok */
#define __NR_move_pages		317 /* mmu */
#define __NR_getcpu		318 /* ok */
#define __NR_epoll_pwait	319 /* ok */
#define __NR_utimensat		320 /* ok */
#define __NR_signalfd		321 /* ok */
#define __NR_timerfd_create	322 /* ok */
#define __NR_eventfd		323 /* ok */
#define __NR_fallocate		324 /* ok */
#define __NR_semtimedop		325 /* ok - semaphore group */
#define __NR_timerfd_settime	326 /* ok */
#define __NR_timerfd_gettime	327 /* ok */
/* sysv ipc syscalls */
#define __NR_semctl		328 /* ok */
#define __NR_semget		329 /* ok */
#define __NR_semop		330 /* ok */
#define __NR_msgctl		331 /* ok */
#define __NR_msgget		332 /* ok */
#define __NR_msgrcv		333 /* ok */
#define __NR_msgsnd		334 /* ok */
#define __NR_shmat		335 /* ok */
#define __NR_shmctl		336 /* ok */
#define __NR_shmdt		337 /* ok */
#define __NR_shmget		338 /* ok */


#define __NR_signalfd4		339 /* new */
#define __NR_eventfd2		340 /* new */
#define __NR_epoll_create1	341 /* new */
#define __NR_dup3		342 /* new */
#define __NR_pipe2		343 /* new */
#define __NR_inotify_init1	344 /* new */
#define __NR_socket		345 /* new */
#define __NR_socketpair		346 /* new */
#define __NR_bind		347 /* new */
#define __NR_listen		348 /* new */
#define __NR_accept		349 /* new */
#define __NR_connect		350 /* new */
#define __NR_getsockname	351 /* new */
#define __NR_getpeername	352 /* new */
#define __NR_sendto		353 /* new */
#define __NR_send		354 /* new */
#define __NR_recvfrom		355 /* new */
#define __NR_recv		356 /* new */
#define __NR_setsockopt		357 /* new */
#define __NR_getsockopt		358 /* new */
#define __NR_shutdown		359 /* new */
#define __NR_sendmsg		360 /* new */
#define __NR_recvmsg		361 /* new */
#define __NR_accept4		362 /* new */
#define __NR_preadv		363 /* new */
#define __NR_pwritev		364 /* new */
#define __NR_rt_tgsigqueueinfo	365 /* new */
#define __NR_perf_event_open	366 /* new */
#define __NR_recvmmsg		367 /* new */
#define __NR_fanotify_init	368
#define __NR_fanotify_mark	369
#define __NR_prlimit64		370
#define __NR_name_to_handle_at	371
#define __NR_open_by_handle_at	372
#define __NR_clock_adjtime	373
#define __NR_syncfs		374
#define __NR_setns		375
#define __NR_sendmmsg		376
#define __NR_process_vm_readv	377
#define __NR_process_vm_writev	378
#define __NR_kcmp		379
#define __NR_finit_module	380
#define __NR_sched_setattr	381
#define __NR_sched_getattr	382
#define __NR_renameat2		383
#define __NR_seccomp		384
#define __NR_getrandom		385
#define __NR_memfd_create	386
#define __NR_bpf		387
#define __NR_execveat		388
#define __NR_userfaultfd	389
#define __NR_membarrier		390
#define __NR_mlock2		391
#define __NR_copy_file_range	392
#define __NR_preadv2		393
#define __NR_pwritev2		394
#define __NR_pkey_mprotect	395
#define __NR_pkey_alloc		396
#define __NR_pkey_free		397
#define __NR_statx		398

#endif /* _UAPI_ASM_MICROBLAZE_UNISTD_H */
