/*
 * AArch32 (compat) system call definitions.
 *
 * Copyright (C) 2001-2005 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SYSCALL
#define __SYSCALL(x, y)
#endif

__SYSCALL(0,   sys_restart_syscall)
__SYSCALL(1,   sys_exit)
__SYSCALL(2,   sys_fork)
__SYSCALL(3,   sys_read)
__SYSCALL(4,   sys_write)
__SYSCALL(5,   compat_sys_open)
__SYSCALL(6,   sys_close)
__SYSCALL(7,   sys_ni_syscall)			/* 7 was sys_waitpid */
__SYSCALL(8,   sys_creat)
__SYSCALL(9,   sys_link)
__SYSCALL(10,  sys_unlink)
__SYSCALL(11,  compat_sys_execve)
__SYSCALL(12,  sys_chdir)
__SYSCALL(13,  sys_ni_syscall)			/* 13 was sys_time */
__SYSCALL(14,  sys_mknod)
__SYSCALL(15,  sys_chmod)
__SYSCALL(16,  sys_lchown16)
__SYSCALL(17,  sys_ni_syscall)			/* 17 was sys_break */
__SYSCALL(18,  sys_ni_syscall)			/* 18 was sys_stat */
__SYSCALL(19,  compat_sys_lseek_wrapper)
__SYSCALL(20,  sys_getpid)
__SYSCALL(21,  compat_sys_mount)
__SYSCALL(22,  sys_ni_syscall)			/* 22 was sys_umount */
__SYSCALL(23,  sys_setuid16)
__SYSCALL(24,  sys_getuid16)
__SYSCALL(25,  sys_ni_syscall)			/* 25 was sys_stime */
__SYSCALL(26,  compat_sys_ptrace)
__SYSCALL(27,  sys_ni_syscall)			/* 27 was sys_alarm */
__SYSCALL(28,  sys_ni_syscall)			/* 28 was sys_fstat */
__SYSCALL(29,  sys_pause)
__SYSCALL(30,  sys_ni_syscall)			/* 30 was sys_utime */
__SYSCALL(31,  sys_ni_syscall)			/* 31 was sys_stty */
__SYSCALL(32,  sys_ni_syscall)			/* 32 was sys_gtty */
__SYSCALL(33,  sys_access)
__SYSCALL(34,  sys_nice)
__SYSCALL(35,  sys_ni_syscall)			/* 35 was sys_ftime */
__SYSCALL(36,  sys_sync)
__SYSCALL(37,  sys_kill)
__SYSCALL(38,  sys_rename)
__SYSCALL(39,  sys_mkdir)
__SYSCALL(40,  sys_rmdir)
__SYSCALL(41,  sys_dup)
__SYSCALL(42,  sys_pipe)
__SYSCALL(43,  compat_sys_times)
__SYSCALL(44,  sys_ni_syscall)			/* 44 was sys_prof */
__SYSCALL(45,  sys_brk)
__SYSCALL(46,  sys_setgid16)
__SYSCALL(47,  sys_getgid16)
__SYSCALL(48,  sys_ni_syscall)			/* 48 was sys_signal */
__SYSCALL(49,  sys_geteuid16)
__SYSCALL(50,  sys_getegid16)
__SYSCALL(51,  sys_acct)
__SYSCALL(52,  sys_umount)
__SYSCALL(53,  sys_ni_syscall)			/* 53 was sys_lock */
__SYSCALL(54,  compat_sys_ioctl)
__SYSCALL(55,  compat_sys_fcntl)
__SYSCALL(56,  sys_ni_syscall)			/* 56 was sys_mpx */
__SYSCALL(57,  sys_setpgid)
__SYSCALL(58,  sys_ni_syscall)			/* 58 was sys_ulimit */
__SYSCALL(59,  sys_ni_syscall)			/* 59 was sys_olduname */
__SYSCALL(60,  sys_umask)
__SYSCALL(61,  sys_chroot)
__SYSCALL(62,  compat_sys_ustat)
__SYSCALL(63,  sys_dup2)
__SYSCALL(64,  sys_getppid)
__SYSCALL(65,  sys_getpgrp)
__SYSCALL(66,  sys_setsid)
__SYSCALL(67,  compat_sys_sigaction)
__SYSCALL(68,  sys_ni_syscall)			/* 68 was sys_sgetmask */
__SYSCALL(69,  sys_ni_syscall)			/* 69 was sys_ssetmask */
__SYSCALL(70,  sys_setreuid16)
__SYSCALL(71,  sys_setregid16)
__SYSCALL(72,  compat_sys_sigsuspend)
__SYSCALL(73,  compat_sys_sigpending)
__SYSCALL(74,  sys_sethostname)
__SYSCALL(75,  compat_sys_setrlimit)
__SYSCALL(76,  sys_ni_syscall)			/* 76 was compat_sys_getrlimit */
__SYSCALL(77,  compat_sys_getrusage)
__SYSCALL(78,  compat_sys_gettimeofday)
__SYSCALL(79,  compat_sys_settimeofday)
__SYSCALL(80,  sys_getgroups16)
__SYSCALL(81,  sys_setgroups16)
__SYSCALL(82,  sys_ni_syscall)			/* 82 was compat_sys_select */
__SYSCALL(83,  sys_symlink)
__SYSCALL(84,  sys_ni_syscall)			/* 84 was sys_lstat */
__SYSCALL(85,  sys_readlink)
__SYSCALL(86,  sys_uselib)
__SYSCALL(87,  sys_swapon)
__SYSCALL(88,  sys_reboot)
__SYSCALL(89,  sys_ni_syscall)			/* 89 was sys_readdir */
__SYSCALL(90,  sys_ni_syscall)			/* 90 was sys_mmap */
__SYSCALL(91,  sys_munmap)
__SYSCALL(92,  sys_truncate)
__SYSCALL(93,  sys_ftruncate)
__SYSCALL(94,  sys_fchmod)
__SYSCALL(95,  sys_fchown16)
__SYSCALL(96,  sys_getpriority)
__SYSCALL(97,  sys_setpriority)
__SYSCALL(98,  sys_ni_syscall)			/* 98 was sys_profil */
__SYSCALL(99,  compat_sys_statfs)
__SYSCALL(100, compat_sys_fstatfs)
__SYSCALL(101, sys_ni_syscall)			/* 101 was sys_ioperm */
__SYSCALL(102, sys_ni_syscall)			/* 102 was sys_socketcall */
__SYSCALL(103, sys_syslog)
__SYSCALL(104, compat_sys_setitimer)
__SYSCALL(105, compat_sys_getitimer)
__SYSCALL(106, compat_sys_newstat)
__SYSCALL(107, compat_sys_newlstat)
__SYSCALL(108, compat_sys_newfstat)
__SYSCALL(109, sys_ni_syscall)			/* 109 was sys_uname */
__SYSCALL(110, sys_ni_syscall)			/* 110 was sys_iopl */
__SYSCALL(111, sys_vhangup)
__SYSCALL(112, sys_ni_syscall)			/* 112 was sys_idle */
__SYSCALL(113, sys_ni_syscall)			/* 113 was sys_syscall */
__SYSCALL(114, compat_sys_wait4)
__SYSCALL(115, sys_swapoff)
__SYSCALL(116, compat_sys_sysinfo)
__SYSCALL(117, sys_ni_syscall)			/* 117 was sys_ipc */
__SYSCALL(118, sys_fsync)
__SYSCALL(119, compat_sys_sigreturn_wrapper)
__SYSCALL(120, sys_clone)
__SYSCALL(121, sys_setdomainname)
__SYSCALL(122, sys_newuname)
__SYSCALL(123, sys_ni_syscall)			/* 123 was sys_modify_ldt */
__SYSCALL(124, compat_sys_adjtimex)
__SYSCALL(125, sys_mprotect)
__SYSCALL(126, compat_sys_sigprocmask)
__SYSCALL(127, sys_ni_syscall)			/* 127 was sys_create_module */
__SYSCALL(128, sys_init_module)
__SYSCALL(129, sys_delete_module)
__SYSCALL(130, sys_ni_syscall)			/* 130 was sys_get_kernel_syms */
__SYSCALL(131, sys_quotactl)
__SYSCALL(132, sys_getpgid)
__SYSCALL(133, sys_fchdir)
__SYSCALL(134, sys_bdflush)
__SYSCALL(135, sys_sysfs)
__SYSCALL(136, sys_personality)
__SYSCALL(137, sys_ni_syscall)			/* 137 was sys_afs_syscall */
__SYSCALL(138, sys_setfsuid16)
__SYSCALL(139, sys_setfsgid16)
__SYSCALL(140, sys_llseek)
__SYSCALL(141, compat_sys_getdents)
__SYSCALL(142, compat_sys_select)
__SYSCALL(143, sys_flock)
__SYSCALL(144, sys_msync)
__SYSCALL(145, compat_sys_readv)
__SYSCALL(146, compat_sys_writev)
__SYSCALL(147, sys_getsid)
__SYSCALL(148, sys_fdatasync)
__SYSCALL(149, compat_sys_sysctl)
__SYSCALL(150, sys_mlock)
__SYSCALL(151, sys_munlock)
__SYSCALL(152, sys_mlockall)
__SYSCALL(153, sys_munlockall)
__SYSCALL(154, sys_sched_setparam)
__SYSCALL(155, sys_sched_getparam)
__SYSCALL(156, sys_sched_setscheduler)
__SYSCALL(157, sys_sched_getscheduler)
__SYSCALL(158, sys_sched_yield)
__SYSCALL(159, sys_sched_get_priority_max)
__SYSCALL(160, sys_sched_get_priority_min)
__SYSCALL(161, compat_sys_sched_rr_get_interval)
__SYSCALL(162, compat_sys_nanosleep)
__SYSCALL(163, sys_mremap)
__SYSCALL(164, sys_setresuid16)
__SYSCALL(165, sys_getresuid16)
__SYSCALL(166, sys_ni_syscall)			/* 166 was sys_vm86 */
__SYSCALL(167, sys_ni_syscall)			/* 167 was sys_query_module */
__SYSCALL(168, sys_poll)
__SYSCALL(169, sys_ni_syscall)
__SYSCALL(170, sys_setresgid16)
__SYSCALL(171, sys_getresgid16)
__SYSCALL(172, sys_prctl)
__SYSCALL(173, compat_sys_rt_sigreturn_wrapper)
__SYSCALL(174, compat_sys_rt_sigaction)
__SYSCALL(175, compat_sys_rt_sigprocmask)
__SYSCALL(176, compat_sys_rt_sigpending)
__SYSCALL(177, compat_sys_rt_sigtimedwait)
__SYSCALL(178, compat_sys_rt_sigqueueinfo)
__SYSCALL(179, compat_sys_rt_sigsuspend)
__SYSCALL(180, compat_sys_pread64_wrapper)
__SYSCALL(181, compat_sys_pwrite64_wrapper)
__SYSCALL(182, sys_chown16)
__SYSCALL(183, sys_getcwd)
__SYSCALL(184, sys_capget)
__SYSCALL(185, sys_capset)
__SYSCALL(186, compat_sys_sigaltstack_wrapper)
__SYSCALL(187, compat_sys_sendfile)
__SYSCALL(188, sys_ni_syscall)			/* 188 reserved */
__SYSCALL(189, sys_ni_syscall)			/* 189 reserved */
__SYSCALL(190, sys_vfork)
__SYSCALL(191, compat_sys_getrlimit)		/* SuS compliant getrlimit */
__SYSCALL(192, sys_mmap_pgoff)
__SYSCALL(193, compat_sys_truncate64_wrapper)
__SYSCALL(194, compat_sys_ftruncate64_wrapper)
__SYSCALL(195, sys_stat64)
__SYSCALL(196, sys_lstat64)
__SYSCALL(197, sys_fstat64)
__SYSCALL(198, sys_lchown)
__SYSCALL(199, sys_getuid)
__SYSCALL(200, sys_getgid)
__SYSCALL(201, sys_geteuid)
__SYSCALL(202, sys_getegid)
__SYSCALL(203, sys_setreuid)
__SYSCALL(204, sys_setregid)
__SYSCALL(205, sys_getgroups)
__SYSCALL(206, sys_setgroups)
__SYSCALL(207, sys_fchown)
__SYSCALL(208, sys_setresuid)
__SYSCALL(209, sys_getresuid)
__SYSCALL(210, sys_setresgid)
__SYSCALL(211, sys_getresgid)
__SYSCALL(212, sys_chown)
__SYSCALL(213, sys_setuid)
__SYSCALL(214, sys_setgid)
__SYSCALL(215, sys_setfsuid)
__SYSCALL(216, sys_setfsgid)
__SYSCALL(217, compat_sys_getdents64)
__SYSCALL(218, sys_pivot_root)
__SYSCALL(219, sys_mincore)
__SYSCALL(220, sys_madvise)
__SYSCALL(221, compat_sys_fcntl64)
__SYSCALL(222, sys_ni_syscall)			/* 222 for tux */
__SYSCALL(223, sys_ni_syscall)			/* 223 is unused */
__SYSCALL(224, sys_gettid)
__SYSCALL(225, compat_sys_readahead_wrapper)
__SYSCALL(226, sys_setxattr)
__SYSCALL(227, sys_lsetxattr)
__SYSCALL(228, sys_fsetxattr)
__SYSCALL(229, sys_getxattr)
__SYSCALL(230, sys_lgetxattr)
__SYSCALL(231, sys_fgetxattr)
__SYSCALL(232, sys_listxattr)
__SYSCALL(233, sys_llistxattr)
__SYSCALL(234, sys_flistxattr)
__SYSCALL(235, sys_removexattr)
__SYSCALL(236, sys_lremovexattr)
__SYSCALL(237, sys_fremovexattr)
__SYSCALL(238, sys_tkill)
__SYSCALL(239, sys_sendfile64)
__SYSCALL(240, compat_sys_futex)
__SYSCALL(241, compat_sys_sched_setaffinity)
__SYSCALL(242, compat_sys_sched_getaffinity)
__SYSCALL(243, compat_sys_io_setup)
__SYSCALL(244, sys_io_destroy)
__SYSCALL(245, compat_sys_io_getevents)
__SYSCALL(246, compat_sys_io_submit)
__SYSCALL(247, sys_io_cancel)
__SYSCALL(248, sys_exit_group)
__SYSCALL(249, compat_sys_lookup_dcookie)
__SYSCALL(250, sys_epoll_create)
__SYSCALL(251, sys_epoll_ctl)
__SYSCALL(252, sys_epoll_wait)
__SYSCALL(253, sys_remap_file_pages)
__SYSCALL(254, sys_ni_syscall)			/* 254 for set_thread_area */
__SYSCALL(255, sys_ni_syscall)			/* 255 for get_thread_area */
__SYSCALL(256, sys_set_tid_address)
__SYSCALL(257, compat_sys_timer_create)
__SYSCALL(258, compat_sys_timer_settime)
__SYSCALL(259, compat_sys_timer_gettime)
__SYSCALL(260, sys_timer_getoverrun)
__SYSCALL(261, sys_timer_delete)
__SYSCALL(262, compat_sys_clock_settime)
__SYSCALL(263, compat_sys_clock_gettime)
__SYSCALL(264, compat_sys_clock_getres)
__SYSCALL(265, compat_sys_clock_nanosleep)
__SYSCALL(266, compat_sys_statfs64_wrapper)
__SYSCALL(267, compat_sys_fstatfs64_wrapper)
__SYSCALL(268, sys_tgkill)
__SYSCALL(269, compat_sys_utimes)
__SYSCALL(270, compat_sys_fadvise64_64_wrapper)
__SYSCALL(271, sys_pciconfig_iobase)
__SYSCALL(272, sys_pciconfig_read)
__SYSCALL(273, sys_pciconfig_write)
__SYSCALL(274, compat_sys_mq_open)
__SYSCALL(275, sys_mq_unlink)
__SYSCALL(276, compat_sys_mq_timedsend)
__SYSCALL(277, compat_sys_mq_timedreceive)
__SYSCALL(278, compat_sys_mq_notify)
__SYSCALL(279, compat_sys_mq_getsetattr)
__SYSCALL(280, compat_sys_waitid)
__SYSCALL(281, sys_socket)
__SYSCALL(282, sys_bind)
__SYSCALL(283, sys_connect)
__SYSCALL(284, sys_listen)
__SYSCALL(285, sys_accept)
__SYSCALL(286, sys_getsockname)
__SYSCALL(287, sys_getpeername)
__SYSCALL(288, sys_socketpair)
__SYSCALL(289, sys_send)
__SYSCALL(290, sys_sendto)
__SYSCALL(291, compat_sys_recv)
__SYSCALL(292, compat_sys_recvfrom)
__SYSCALL(293, sys_shutdown)
__SYSCALL(294, compat_sys_setsockopt)
__SYSCALL(295, compat_sys_getsockopt)
__SYSCALL(296, compat_sys_sendmsg)
__SYSCALL(297, compat_sys_recvmsg)
__SYSCALL(298, sys_semop)
__SYSCALL(299, sys_semget)
__SYSCALL(300, compat_sys_semctl)
__SYSCALL(301, compat_sys_msgsnd)
__SYSCALL(302, compat_sys_msgrcv)
__SYSCALL(303, sys_msgget)
__SYSCALL(304, compat_sys_msgctl)
__SYSCALL(305, compat_sys_shmat)
__SYSCALL(306, sys_shmdt)
__SYSCALL(307, sys_shmget)
__SYSCALL(308, compat_sys_shmctl)
__SYSCALL(309, sys_add_key)
__SYSCALL(310, sys_request_key)
__SYSCALL(311, compat_sys_keyctl)
__SYSCALL(312, compat_sys_semtimedop)
__SYSCALL(313, sys_ni_syscall)
__SYSCALL(314, sys_ioprio_set)
__SYSCALL(315, sys_ioprio_get)
__SYSCALL(316, sys_inotify_init)
__SYSCALL(317, sys_inotify_add_watch)
__SYSCALL(318, sys_inotify_rm_watch)
__SYSCALL(319, compat_sys_mbind)
__SYSCALL(320, compat_sys_get_mempolicy)
__SYSCALL(321, compat_sys_set_mempolicy)
__SYSCALL(322, compat_sys_openat)
__SYSCALL(323, sys_mkdirat)
__SYSCALL(324, sys_mknodat)
__SYSCALL(325, sys_fchownat)
__SYSCALL(326, compat_sys_futimesat)
__SYSCALL(327, sys_fstatat64)
__SYSCALL(328, sys_unlinkat)
__SYSCALL(329, sys_renameat)
__SYSCALL(330, sys_linkat)
__SYSCALL(331, sys_symlinkat)
__SYSCALL(332, sys_readlinkat)
__SYSCALL(333, sys_fchmodat)
__SYSCALL(334, sys_faccessat)
__SYSCALL(335, compat_sys_pselect6)
__SYSCALL(336, compat_sys_ppoll)
__SYSCALL(337, sys_unshare)
__SYSCALL(338, compat_sys_set_robust_list)
__SYSCALL(339, compat_sys_get_robust_list)
__SYSCALL(340, sys_splice)
__SYSCALL(341, compat_sys_sync_file_range2_wrapper)
__SYSCALL(342, sys_tee)
__SYSCALL(343, compat_sys_vmsplice)
__SYSCALL(344, compat_sys_move_pages)
__SYSCALL(345, sys_getcpu)
__SYSCALL(346, compat_sys_epoll_pwait)
__SYSCALL(347, compat_sys_kexec_load)
__SYSCALL(348, compat_sys_utimensat)
__SYSCALL(349, compat_sys_signalfd)
__SYSCALL(350, sys_timerfd_create)
__SYSCALL(351, sys_eventfd)
__SYSCALL(352, compat_sys_fallocate_wrapper)
__SYSCALL(353, compat_sys_timerfd_settime)
__SYSCALL(354, compat_sys_timerfd_gettime)
__SYSCALL(355, compat_sys_signalfd4)
__SYSCALL(356, sys_eventfd2)
__SYSCALL(357, sys_epoll_create1)
__SYSCALL(358, sys_dup3)
__SYSCALL(359, sys_pipe2)
__SYSCALL(360, sys_inotify_init1)
__SYSCALL(361, compat_sys_preadv)
__SYSCALL(362, compat_sys_pwritev)
__SYSCALL(363, compat_sys_rt_tgsigqueueinfo)
__SYSCALL(364, sys_perf_event_open)
__SYSCALL(365, compat_sys_recvmmsg)
__SYSCALL(366, sys_accept4)
__SYSCALL(367, sys_fanotify_init)
__SYSCALL(368, compat_sys_fanotify_mark_wrapper)
__SYSCALL(369, sys_prlimit64)
__SYSCALL(370, sys_name_to_handle_at)
__SYSCALL(371, compat_sys_open_by_handle_at)
__SYSCALL(372, compat_sys_clock_adjtime)
__SYSCALL(373, sys_syncfs)
__SYSCALL(374, compat_sys_sendmmsg)
__SYSCALL(375, sys_setns)
__SYSCALL(376, compat_sys_process_vm_readv)
__SYSCALL(377, compat_sys_process_vm_writev)
__SYSCALL(378, sys_ni_syscall)			/* 378 for kcmp */

#define __NR_compat_syscalls		379

/*
 * Compat syscall numbers used by the AArch64 kernel.
 */
#define __NR_compat_restart_syscall	0
#define __NR_compat_sigreturn		119
#define __NR_compat_rt_sigreturn	173


/*
 * The following SVCs are ARM private.
 */
#define __ARM_NR_COMPAT_BASE		0x0f0000
#define __ARM_NR_compat_cacheflush	(__ARM_NR_COMPAT_BASE+2)
#define __ARM_NR_compat_set_tls		(__ARM_NR_COMPAT_BASE+5)
