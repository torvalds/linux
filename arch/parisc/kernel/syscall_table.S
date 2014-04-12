/*    System Call Table
 *
 *    Copyright (C) 1999-2004 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2000-2001 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2000 Alan Modra <amodra at parisc-linux.org>
 *    Copyright (C) 2000-2003 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2000 Philipp Rumpf <prumpf with tux.org>
 *    Copyright (C) 2000 Michael Ang <mang with subcarrier.org>
 *    Copyright (C) 2000 David Huggins-Daines <dhd with pobox.org>
 *    Copyright (C) 2000 Grant Grundler <grundler at parisc-linux.org>
 *    Copyright (C) 2001 Richard Hirst <rhirst with parisc-linux.org>
 *    Copyright (C) 2001-2002 Ryan Bradetich <rbrad at parisc-linux.org>
 *    Copyright (C) 2001-2007 Helge Deller <deller at parisc-linux.org>
 *    Copyright (C) 2000-2001 Thomas Bogendoerfer <tsbogend at parisc-linux.org>
 *    Copyright (C) 2002 Randolph Chung <tausq with parisc-linux.org>
 *    Copyright (C) 2005-2006 Kyle McMartin <kyle at parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if defined(CONFIG_64BIT) && !defined(SYSCALL_TABLE_64BIT)
/* Use ENTRY_SAME for 32-bit syscalls which are the same on wide and
 * narrow palinux.  Use ENTRY_DIFF for those where a 32-bit specific
 * implementation is required on wide palinux.  Use ENTRY_COMP where
 * the compatibility layer has a useful 32-bit implementation.
 */
#define ENTRY_SAME(_name_) .dword sys_##_name_
#define ENTRY_DIFF(_name_) .dword sys32_##_name_
#define ENTRY_UHOH(_name_) .dword sys32_##unimplemented
#define ENTRY_OURS(_name_) .dword parisc_##_name_
#define ENTRY_COMP(_name_) .dword compat_sys_##_name_
#elif defined(CONFIG_64BIT) && defined(SYSCALL_TABLE_64BIT)
#define ENTRY_SAME(_name_) .dword sys_##_name_
#define ENTRY_DIFF(_name_) .dword sys_##_name_
#define ENTRY_UHOH(_name_) .dword sys_##_name_
#define ENTRY_OURS(_name_) .dword sys_##_name_
#define ENTRY_COMP(_name_) .dword sys_##_name_
#else
#define ENTRY_SAME(_name_) .word sys_##_name_
#define ENTRY_DIFF(_name_) .word sys_##_name_
#define ENTRY_UHOH(_name_) .word sys_##_name_
#define ENTRY_OURS(_name_) .word parisc_##_name_
#define ENTRY_COMP(_name_) .word sys_##_name_
#endif

	ENTRY_SAME(restart_syscall)	/* 0 */
	ENTRY_SAME(exit)
	ENTRY_SAME(fork_wrapper)
	ENTRY_SAME(read)
	ENTRY_SAME(write)
	ENTRY_SAME(open)		/* 5 */
	ENTRY_SAME(close)
	ENTRY_SAME(waitpid)
	ENTRY_SAME(creat)
	ENTRY_SAME(link)
	ENTRY_SAME(unlink)		/* 10 */
	ENTRY_DIFF(execve_wrapper)
	ENTRY_SAME(chdir)
	/* See comments in kernel/time.c!!! Maybe we don't need this? */
	ENTRY_COMP(time)
	ENTRY_SAME(mknod)
	ENTRY_SAME(chmod)		/* 15 */
	ENTRY_SAME(lchown)
	ENTRY_SAME(socket)
	/* struct stat is MAYBE identical wide and narrow ?? */
	ENTRY_COMP(newstat)
	ENTRY_DIFF(lseek)
	ENTRY_SAME(getpid)		/* 20 */
	/* the 'void * data' parameter may need re-packing in wide */
	ENTRY_COMP(mount)
	/* concerned about struct sockaddr in wide/narrow */
	/* ---> I think sockaddr is OK unless the compiler packs the struct */
	/*      differently to align the char array */
	ENTRY_SAME(bind)
	ENTRY_SAME(setuid)
	ENTRY_SAME(getuid)
	ENTRY_COMP(stime)		/* 25 */
	ENTRY_COMP(ptrace)
	ENTRY_SAME(alarm)
	/* see stat comment */
	ENTRY_COMP(newfstat)
	ENTRY_SAME(pause)
	/* struct utimbuf uses time_t which might vary */
	ENTRY_COMP(utime)		/* 30 */
	/* struct sockaddr... */
	ENTRY_SAME(connect)
	ENTRY_SAME(listen)
	ENTRY_SAME(access)
	ENTRY_SAME(nice)
	/* struct sockaddr... */
	ENTRY_SAME(accept)		/* 35 */
	ENTRY_SAME(sync)
	ENTRY_SAME(kill)
	ENTRY_SAME(rename)
	ENTRY_SAME(mkdir)
	ENTRY_SAME(rmdir)		/* 40 */
	ENTRY_SAME(dup)
	ENTRY_SAME(pipe)
	ENTRY_COMP(times)
	/* struct sockaddr... */
	ENTRY_SAME(getsockname)
	/* it seems possible brk() could return a >4G pointer... */
	ENTRY_SAME(brk)			/* 45 */
	ENTRY_SAME(setgid)
	ENTRY_SAME(getgid)
	ENTRY_SAME(signal)
	ENTRY_SAME(geteuid)
	ENTRY_SAME(getegid)		/* 50 */
	ENTRY_SAME(acct)
	ENTRY_SAME(umount)
	/* struct sockaddr... */
	ENTRY_SAME(getpeername)
	ENTRY_COMP(ioctl)
	ENTRY_COMP(fcntl)		/* 55 */
	ENTRY_SAME(socketpair)
	ENTRY_SAME(setpgid)
	ENTRY_SAME(send)
	ENTRY_SAME(newuname)
	ENTRY_SAME(umask)		/* 60 */
	ENTRY_SAME(chroot)
	ENTRY_COMP(ustat)
	ENTRY_SAME(dup2)
	ENTRY_SAME(getppid)
	ENTRY_SAME(getpgrp)		/* 65 */
	ENTRY_SAME(setsid)
	ENTRY_SAME(pivot_root)
	/* I don't like this */
	ENTRY_UHOH(sgetmask)
	ENTRY_UHOH(ssetmask)
	ENTRY_SAME(setreuid)		/* 70 */
	ENTRY_SAME(setregid)
	ENTRY_SAME(mincore)
	ENTRY_COMP(sigpending)
	ENTRY_SAME(sethostname)
	/* Following 3 have linux-common-code structs containing longs -( */
	ENTRY_COMP(setrlimit)		/* 75 */
	ENTRY_COMP(getrlimit)
	ENTRY_COMP(getrusage)
	/* struct timeval and timezone are maybe?? consistent wide and narrow */
	ENTRY_COMP(gettimeofday)
	ENTRY_COMP(settimeofday)
	ENTRY_SAME(getgroups)		/* 80 */
	ENTRY_SAME(setgroups)
	/* struct socketaddr... */
	ENTRY_SAME(sendto)
	ENTRY_SAME(symlink)
	/* see stat comment */
	ENTRY_COMP(newlstat)
	ENTRY_SAME(readlink)		/* 85 */
	ENTRY_SAME(ni_syscall)	/* was uselib */
	ENTRY_SAME(swapon)
	ENTRY_SAME(reboot)
	ENTRY_SAME(mmap2)
	ENTRY_SAME(mmap)		/* 90 */
	ENTRY_SAME(munmap)
	ENTRY_SAME(truncate)
	ENTRY_SAME(ftruncate)
	ENTRY_SAME(fchmod)
	ENTRY_SAME(fchown)		/* 95 */
	ENTRY_SAME(getpriority)
	ENTRY_SAME(setpriority)
	ENTRY_SAME(recv)
	ENTRY_COMP(statfs)
	ENTRY_COMP(fstatfs)		/* 100 */
	ENTRY_SAME(stat64)
	ENTRY_SAME(ni_syscall)	/* was socketcall */
	ENTRY_SAME(syslog)
	/* even though manpage says struct timeval contains longs, ours has
	 * time_t and suseconds_t -- both of which are safe wide/narrow */
	ENTRY_COMP(setitimer)
	ENTRY_COMP(getitimer)		/* 105 */
	ENTRY_SAME(capget)
	ENTRY_SAME(capset)
	ENTRY_OURS(pread64)
	ENTRY_OURS(pwrite64)
	ENTRY_SAME(getcwd)		/* 110 */
	ENTRY_SAME(vhangup)
	ENTRY_SAME(fstat64)
	ENTRY_SAME(vfork_wrapper)
	/* struct rusage contains longs... */
	ENTRY_COMP(wait4)
	ENTRY_SAME(swapoff)		/* 115 */
	ENTRY_COMP(sysinfo)
	ENTRY_SAME(shutdown)
	ENTRY_SAME(fsync)
	ENTRY_SAME(madvise)
	ENTRY_SAME(clone_wrapper)	/* 120 */
	ENTRY_SAME(setdomainname)
	ENTRY_DIFF(sendfile)
	/* struct sockaddr... */
	ENTRY_SAME(recvfrom)
	/* struct timex contains longs */
	ENTRY_COMP(adjtimex)
	ENTRY_SAME(mprotect)		/* 125 */
	/* old_sigset_t forced to 32 bits.  Beware glibc sigset_t */
	ENTRY_COMP(sigprocmask)
	ENTRY_SAME(ni_syscall)	/* create_module */
	ENTRY_SAME(init_module)
	ENTRY_SAME(delete_module)
	ENTRY_SAME(ni_syscall)		/* 130: get_kernel_syms */
	/* time_t inside struct dqblk */
	ENTRY_SAME(quotactl)
	ENTRY_SAME(getpgid)
	ENTRY_SAME(fchdir)
	ENTRY_SAME(bdflush)
	ENTRY_SAME(sysfs)		/* 135 */
	ENTRY_OURS(personality)
	ENTRY_SAME(ni_syscall)	/* for afs_syscall */
	ENTRY_SAME(setfsuid)
	ENTRY_SAME(setfsgid)
	/* I think this might work */
	ENTRY_SAME(llseek)		/* 140 */
	ENTRY_COMP(getdents)
	/* it is POSSIBLE that select will be OK because even though fd_set
	 * contains longs, the macros and sizes are clever. */
	ENTRY_COMP(select)
	ENTRY_SAME(flock)
	ENTRY_SAME(msync)
	/* struct iovec contains pointers */
	ENTRY_COMP(readv)		/* 145 */
	ENTRY_COMP(writev)
	ENTRY_SAME(getsid)
	ENTRY_SAME(fdatasync)
	/* struct __sysctl_args is a mess */
	ENTRY_COMP(sysctl)
	ENTRY_SAME(mlock)		/* 150 */
	ENTRY_SAME(munlock)
	ENTRY_SAME(mlockall)
	ENTRY_SAME(munlockall)
	/* struct sched_param is ok for now */
	ENTRY_SAME(sched_setparam)
	ENTRY_SAME(sched_getparam)	/* 155 */
	ENTRY_SAME(sched_setscheduler)
	ENTRY_SAME(sched_getscheduler)
	ENTRY_SAME(sched_yield)
	ENTRY_SAME(sched_get_priority_max)
	ENTRY_SAME(sched_get_priority_min)	/* 160 */
	/* These 2 would've worked if someone had defined struct timespec
	 * carefully, like timeval for example (which is about the same).
	 * Unfortunately it contains a long :-( */
	ENTRY_DIFF(sched_rr_get_interval)
	ENTRY_COMP(nanosleep)
	ENTRY_SAME(mremap)
	ENTRY_SAME(setresuid)
	ENTRY_SAME(getresuid)		/* 165 */
	ENTRY_DIFF(sigaltstack_wrapper)
	ENTRY_SAME(ni_syscall)		/* query_module */
	ENTRY_SAME(poll)
	/* structs contain pointers and an in_addr... */
	ENTRY_SAME(ni_syscall)		/* was nfsservctl */
	ENTRY_SAME(setresgid)		/* 170 */
	ENTRY_SAME(getresgid)
	ENTRY_SAME(prctl)
	/* signals need a careful review */
	ENTRY_SAME(rt_sigreturn_wrapper)
	ENTRY_DIFF(rt_sigaction)
	ENTRY_DIFF(rt_sigprocmask)	/* 175 */
	ENTRY_DIFF(rt_sigpending)
	ENTRY_COMP(rt_sigtimedwait)
	/* even though the struct siginfo_t is different, it appears like
	 * all the paths use values which should be same wide and narrow.
	 * Also the struct is padded to 128 bytes which means we don't have
	 * to worry about faulting trying to copy in a larger 64-bit
	 * struct from a 32-bit user-space app.
	 */
	ENTRY_COMP(rt_sigqueueinfo)
	ENTRY_COMP(rt_sigsuspend)
	ENTRY_SAME(chown)		/* 180 */
	/* setsockopt() used by iptables: SO_SET_REPLACE/SO_SET_ADD_COUNTERS */
	ENTRY_COMP(setsockopt)
	ENTRY_COMP(getsockopt)
	ENTRY_COMP(sendmsg)
	ENTRY_COMP(recvmsg)
	ENTRY_SAME(semop)		/* 185 */
	ENTRY_SAME(semget)
	ENTRY_DIFF(semctl)
	ENTRY_DIFF(msgsnd)
	ENTRY_DIFF(msgrcv)
	ENTRY_SAME(msgget)		/* 190 */
	ENTRY_SAME(msgctl)
	ENTRY_SAME(shmat)
	ENTRY_SAME(shmdt)
	ENTRY_SAME(shmget)
	ENTRY_SAME(shmctl)		/* 195 */
	ENTRY_SAME(ni_syscall)		/* streams1 */
	ENTRY_SAME(ni_syscall)		/* streams2 */
	ENTRY_SAME(lstat64)
	ENTRY_OURS(truncate64)
	ENTRY_OURS(ftruncate64)		/* 200 */
	ENTRY_SAME(getdents64)
	ENTRY_COMP(fcntl64)
	ENTRY_SAME(ni_syscall)	/* attrctl -- dead */
	ENTRY_SAME(ni_syscall)	/* acl_get -- dead */
	ENTRY_SAME(ni_syscall)		/* 205 (acl_set -- dead) */
	ENTRY_SAME(gettid)
	ENTRY_OURS(readahead)
	ENTRY_SAME(tkill)
	ENTRY_SAME(sendfile64)
	ENTRY_COMP(futex)		/* 210 */
	ENTRY_COMP(sched_setaffinity)
	ENTRY_COMP(sched_getaffinity)
	ENTRY_SAME(ni_syscall)	/* set_thread_area */
	ENTRY_SAME(ni_syscall)	/* get_thread_area */
	ENTRY_SAME(io_setup)		/* 215 */
	ENTRY_SAME(io_destroy)
	ENTRY_SAME(io_getevents)
	ENTRY_SAME(io_submit)
	ENTRY_SAME(io_cancel)
	ENTRY_SAME(alloc_hugepages)	/* 220 */
	ENTRY_SAME(free_hugepages)
	ENTRY_SAME(exit_group)
	ENTRY_DIFF(lookup_dcookie)
	ENTRY_SAME(epoll_create)
	ENTRY_SAME(epoll_ctl)		/* 225 */
	ENTRY_SAME(epoll_wait)
 	ENTRY_SAME(remap_file_pages)
	ENTRY_SAME(semtimedop)
	ENTRY_SAME(mq_open)
	ENTRY_SAME(mq_unlink)		/* 230 */
	ENTRY_SAME(mq_timedsend)
	ENTRY_SAME(mq_timedreceive)
	ENTRY_SAME(mq_notify)
	ENTRY_SAME(mq_getsetattr)
	ENTRY_COMP(waitid)		/* 235 */
	ENTRY_OURS(fadvise64_64)
	ENTRY_SAME(set_tid_address)
	ENTRY_SAME(setxattr)
	ENTRY_SAME(lsetxattr)
	ENTRY_SAME(fsetxattr)		/* 240 */
	ENTRY_SAME(getxattr)
	ENTRY_SAME(lgetxattr)
	ENTRY_SAME(fgetxattr)
	ENTRY_SAME(listxattr)
	ENTRY_SAME(llistxattr)		/* 245 */
	ENTRY_SAME(flistxattr)
	ENTRY_SAME(removexattr)
	ENTRY_SAME(lremovexattr)
	ENTRY_SAME(fremovexattr)
	ENTRY_COMP(timer_create)	/* 250 */
	ENTRY_COMP(timer_settime)
	ENTRY_COMP(timer_gettime)
	ENTRY_SAME(timer_getoverrun)
	ENTRY_SAME(timer_delete)
	ENTRY_COMP(clock_settime)	/* 255 */
	ENTRY_COMP(clock_gettime)
	ENTRY_COMP(clock_getres)
	ENTRY_COMP(clock_nanosleep)
	ENTRY_SAME(tgkill)
	ENTRY_COMP(mbind)		/* 260 */
	ENTRY_COMP(get_mempolicy)
	ENTRY_COMP(set_mempolicy)
	ENTRY_SAME(ni_syscall)	/* 263: reserved for vserver */
	ENTRY_SAME(add_key)
	ENTRY_SAME(request_key)		/* 265 */
	ENTRY_SAME(keyctl)
	ENTRY_SAME(ioprio_set)
	ENTRY_SAME(ioprio_get)
	ENTRY_SAME(inotify_init)
	ENTRY_SAME(inotify_add_watch)	/* 270 */
	ENTRY_SAME(inotify_rm_watch)
	ENTRY_SAME(migrate_pages)
	ENTRY_COMP(pselect6)
	ENTRY_COMP(ppoll)
	ENTRY_COMP(openat)		/* 275 */
	ENTRY_SAME(mkdirat)
	ENTRY_SAME(mknodat)
	ENTRY_SAME(fchownat)
	ENTRY_COMP(futimesat)
	ENTRY_SAME(fstatat64)		/* 280 */
	ENTRY_SAME(unlinkat)
	ENTRY_SAME(renameat)
	ENTRY_SAME(linkat)
	ENTRY_SAME(symlinkat)
	ENTRY_SAME(readlinkat)		/* 285 */
	ENTRY_SAME(fchmodat)
	ENTRY_SAME(faccessat)
	ENTRY_SAME(unshare)
	ENTRY_COMP(set_robust_list)
	ENTRY_COMP(get_robust_list)	/* 290 */
	ENTRY_SAME(splice)
	ENTRY_OURS(sync_file_range)
	ENTRY_SAME(tee)
	ENTRY_COMP(vmsplice)
	ENTRY_COMP(move_pages)		/* 295 */
	ENTRY_SAME(getcpu)
	ENTRY_COMP(epoll_pwait)
	ENTRY_COMP(statfs64)
	ENTRY_COMP(fstatfs64)
	ENTRY_COMP(kexec_load)		/* 300 */
	ENTRY_COMP(utimensat)
	ENTRY_COMP(signalfd)
	ENTRY_SAME(ni_syscall)		/* was timerfd */
	ENTRY_SAME(eventfd)
	ENTRY_COMP(fallocate)		/* 305 */
	ENTRY_SAME(timerfd_create)
	ENTRY_COMP(timerfd_settime)
	ENTRY_COMP(timerfd_gettime)
	ENTRY_COMP(signalfd4)
	ENTRY_SAME(eventfd2)		/* 310 */
	ENTRY_SAME(epoll_create1)
	ENTRY_SAME(dup3)
	ENTRY_SAME(pipe2)
	ENTRY_SAME(inotify_init1)
	ENTRY_COMP(preadv)		/* 315 */
	ENTRY_COMP(pwritev)
	ENTRY_COMP(rt_tgsigqueueinfo)
	ENTRY_SAME(perf_event_open)
	ENTRY_COMP(recvmmsg)
	ENTRY_SAME(accept4)		/* 320 */
	ENTRY_SAME(prlimit64)
	ENTRY_SAME(fanotify_init)
	ENTRY_COMP(fanotify_mark)
	ENTRY_COMP(clock_adjtime)
	ENTRY_SAME(name_to_handle_at)	/* 325 */
	ENTRY_COMP(open_by_handle_at)
	ENTRY_SAME(syncfs)
	ENTRY_SAME(setns)
	ENTRY_COMP(sendmmsg)

	/* Nothing yet */

#undef ENTRY_SAME
#undef ENTRY_DIFF
#undef ENTRY_UHOH
#undef ENTRY_COMP
#undef ENTRY_OURS
