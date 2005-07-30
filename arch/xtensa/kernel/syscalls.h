/*
 * arch/xtensa/kernel/syscalls.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998 by Ralf Baechle
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Changes by Joe Taylor <joe@tensilica.com>
 */

/*
 * This file is being included twice - once to build a list of all
 * syscalls and once to build a table of how many arguments each syscall
 * accepts.  Syscalls that receive a pointer to the saved registers are
 * marked as having zero arguments.
 *
 * The binary compatibility calls are in a separate list.
 *
 * Entry '0' used to be system_call.  It's removed to disable indirect
 * system calls for now so user tasks can't recurse.  See mips'
 * sys_syscall for a comparable example.
 */

SYSCALL(0, 0)		                /* 00 */
SYSCALL(sys_exit, 1)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_read, 3)
SYSCALL(sys_write, 3)
SYSCALL(sys_open, 3)			/* 05 */
SYSCALL(sys_close, 1)
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_creat, 2)
SYSCALL(sys_link, 2)
SYSCALL(sys_unlink, 1)			/* 10 */
SYSCALL(sys_execve, 0)
SYSCALL(sys_chdir, 1)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_mknod, 3)
SYSCALL(sys_chmod, 2)			/* 15 */
SYSCALL(sys_lchown, 3)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_newstat, 2)
SYSCALL(sys_lseek, 3)
SYSCALL(sys_getpid, 0)			/* 20 */
SYSCALL(sys_mount, 5)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_setuid, 1)
SYSCALL(sys_getuid, 0)
SYSCALL(sys_ni_syscall, 1)		/* 25 */
SYSCALL(sys_ptrace, 4)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_newfstat, 2)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_utime, 2)			/* 30 */
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_access, 2)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_ni_syscall, 0)		/* 35 */
SYSCALL(sys_sync, 0)
SYSCALL(sys_kill, 2)
SYSCALL(sys_rename, 2)
SYSCALL(sys_mkdir, 2)
SYSCALL(sys_rmdir, 1)			/* 40 */
SYSCALL(sys_dup, 1)
SYSCALL(sys_pipe, 1)
SYSCALL(sys_times, 1)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_brk, 1)			/* 45 */
SYSCALL(sys_setgid, 1)
SYSCALL(sys_getgid, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_geteuid, 0)
SYSCALL(sys_getegid, 0)			/* 50 */
SYSCALL(sys_acct, 1)
SYSCALL(sys_umount, 2)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ioctl, 3)
SYSCALL(sys_fcntl, 3)			/* 55 */
SYSCALL(sys_ni_syscall, 2)
SYSCALL(sys_setpgid, 2)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_umask, 1)			/* 60 */
SYSCALL(sys_chroot, 1)
SYSCALL(sys_ustat, 2)
SYSCALL(sys_dup2, 2)
SYSCALL(sys_getppid, 0)
SYSCALL(sys_ni_syscall, 0)		/* 65 */
SYSCALL(sys_setsid, 0)
SYSCALL(sys_sigaction, 3)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_setreuid, 2)		/* 70 */
SYSCALL(sys_setregid, 2)
SYSCALL(sys_sigsuspend, 0)
SYSCALL(sys_ni_syscall, 1)
SYSCALL(sys_sethostname, 2)
SYSCALL(sys_setrlimit, 2)		/* 75 */
SYSCALL(sys_getrlimit, 2)
SYSCALL(sys_getrusage, 2)
SYSCALL(sys_gettimeofday, 2)
SYSCALL(sys_settimeofday, 2)
SYSCALL(sys_getgroups, 2)		/* 80 */
SYSCALL(sys_setgroups, 2)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_symlink, 2)
SYSCALL(sys_newlstat, 2)
SYSCALL(sys_readlink, 3)		/* 85 */
SYSCALL(sys_uselib, 1)
SYSCALL(sys_swapon, 2)
SYSCALL(sys_reboot, 3)
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_ni_syscall, 6)		/* 90 */
SYSCALL(sys_munmap, 2)
SYSCALL(sys_truncate, 2)
SYSCALL(sys_ftruncate, 2)
SYSCALL(sys_fchmod, 2)
SYSCALL(sys_fchown, 3)			/* 95 */
SYSCALL(sys_getpriority, 2)
SYSCALL(sys_setpriority, 3)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_statfs, 2)
SYSCALL(sys_fstatfs, 2)			/* 100 */
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_ni_syscall, 2)
SYSCALL(sys_syslog, 3)
SYSCALL(sys_setitimer, 3)
SYSCALL(sys_getitimer, 2)		/* 105 */
SYSCALL(sys_newstat, 2)
SYSCALL(sys_newlstat, 2)
SYSCALL(sys_newfstat, 2)
SYSCALL(sys_uname, 1)
SYSCALL(sys_ni_syscall, 0)		/* 110 */
SYSCALL(sys_vhangup, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_wait4, 4)
SYSCALL(sys_swapoff, 1)			/* 115 */
SYSCALL(sys_sysinfo, 1)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_fsync, 1)
SYSCALL(sys_sigreturn, 0)
SYSCALL(sys_clone, 0)			/* 120 */
SYSCALL(sys_setdomainname, 2)
SYSCALL(sys_newuname, 1)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_adjtimex, 1)
SYSCALL(sys_mprotect, 3)		/* 125 */
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_ni_syscall, 2)
SYSCALL(sys_init_module, 2)
SYSCALL(sys_delete_module, 1)
SYSCALL(sys_ni_syscall, 1)		/* 130 */
SYSCALL(sys_quotactl, 0)
SYSCALL(sys_getpgid, 1)
SYSCALL(sys_fchdir, 1)
SYSCALL(sys_bdflush, 2)
SYSCALL(sys_sysfs, 3)			/* 135 */
SYSCALL(sys_personality, 1)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_setfsuid, 1)
SYSCALL(sys_setfsgid, 1)
SYSCALL(sys_llseek, 5)			/* 140 */
SYSCALL(sys_getdents, 3)
SYSCALL(sys_select, 5)
SYSCALL(sys_flock, 2)
SYSCALL(sys_msync, 3)
SYSCALL(sys_readv, 3)			/* 145 */
SYSCALL(sys_writev, 3)
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_ni_syscall, 3)
SYSCALL(sys_ni_syscall, 4)		/* handled in fast syscall handler. */
SYSCALL(sys_ni_syscall, 0)		/* 150 */
SYSCALL(sys_getsid, 1)
SYSCALL(sys_fdatasync, 1)
SYSCALL(sys_sysctl, 1)
SYSCALL(sys_mlock, 2)
SYSCALL(sys_munlock, 2)			/* 155 */
SYSCALL(sys_mlockall, 1)
SYSCALL(sys_munlockall, 0)
SYSCALL(sys_sched_setparam,2)
SYSCALL(sys_sched_getparam,2)
SYSCALL(sys_sched_setscheduler,3)	/* 160 */
SYSCALL(sys_sched_getscheduler,1)
SYSCALL(sys_sched_yield,0)
SYSCALL(sys_sched_get_priority_max,1)
SYSCALL(sys_sched_get_priority_min,1)
SYSCALL(sys_sched_rr_get_interval,2)	/* 165 */
SYSCALL(sys_nanosleep,2)
SYSCALL(sys_mremap,4)
SYSCALL(sys_accept, 3)
SYSCALL(sys_bind, 3)
SYSCALL(sys_connect, 3)			/* 170 */
SYSCALL(sys_getpeername, 3)
SYSCALL(sys_getsockname, 3)
SYSCALL(sys_getsockopt, 5)
SYSCALL(sys_listen, 2)
SYSCALL(sys_recv, 4)			/* 175 */
SYSCALL(sys_recvfrom, 6)
SYSCALL(sys_recvmsg, 3)
SYSCALL(sys_send, 4)
SYSCALL(sys_sendmsg, 3)
SYSCALL(sys_sendto, 6)			/* 180 */
SYSCALL(sys_setsockopt, 5)
SYSCALL(sys_shutdown, 2)
SYSCALL(sys_socket, 3)
SYSCALL(sys_socketpair, 4)
SYSCALL(sys_setresuid, 3)		/* 185 */
SYSCALL(sys_getresuid, 3)
SYSCALL(sys_ni_syscall, 5)
SYSCALL(sys_poll, 3)
SYSCALL(sys_nfsservctl, 3)
SYSCALL(sys_setresgid, 3)		/* 190 */
SYSCALL(sys_getresgid, 3)
SYSCALL(sys_prctl, 5)
SYSCALL(sys_rt_sigreturn, 0)
SYSCALL(sys_rt_sigaction, 4)
SYSCALL(sys_rt_sigprocmask, 4)		/* 195 */
SYSCALL(sys_rt_sigpending, 2)
SYSCALL(sys_rt_sigtimedwait, 4)
SYSCALL(sys_rt_sigqueueinfo, 3)
SYSCALL(sys_rt_sigsuspend, 0)
SYSCALL(sys_pread64, 5)			/* 200 */
SYSCALL(sys_pwrite64, 5)
SYSCALL(sys_chown, 3)
SYSCALL(sys_getcwd, 2)
SYSCALL(sys_capget, 2)
SYSCALL(sys_capset, 2)			/* 205 */
SYSCALL(sys_sigaltstack, 0)
SYSCALL(sys_sendfile, 4)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_ni_syscall, 0)
SYSCALL(sys_mmap, 6)			/* 210 */
SYSCALL(sys_truncate64, 2)
SYSCALL(sys_ftruncate64, 2)
SYSCALL(sys_stat64, 2)
SYSCALL(sys_lstat64, 2)
SYSCALL(sys_fstat64, 2)			/* 215 */
SYSCALL(sys_pivot_root, 2)
SYSCALL(sys_mincore, 3)
SYSCALL(sys_madvise, 3)
SYSCALL(sys_getdents64, 3)
SYSCALL(sys_ni_syscall, 0)		/* 220 */
