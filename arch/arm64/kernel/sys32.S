/*
 * Compat system call wrappers
 *
 * Copyright (C) 2012 ARM Ltd.
 * Authors: Will Deacon <will.deacon@arm.com>
 *	    Catalin Marinas <catalin.marinas@arm.com>
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

#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/asm-offsets.h>

/*
 * System call wrappers for the AArch32 compatibility layer.
 */
compat_sys_fork_wrapper:
	mov	x0, sp
	b	compat_sys_fork
ENDPROC(compat_sys_fork_wrapper)

compat_sys_vfork_wrapper:
	mov	x0, sp
	b	compat_sys_vfork
ENDPROC(compat_sys_vfork_wrapper)

compat_sys_execve_wrapper:
	mov	x3, sp
	b	compat_sys_execve
ENDPROC(compat_sys_execve_wrapper)

compat_sys_clone_wrapper:
	mov	x5, sp
	b	compat_sys_clone
ENDPROC(compat_sys_clone_wrapper)

compat_sys_sigreturn_wrapper:
	mov	x0, sp
	mov	x27, #0		// prevent syscall restart handling (why)
	b	compat_sys_sigreturn
ENDPROC(compat_sys_sigreturn_wrapper)

compat_sys_rt_sigreturn_wrapper:
	mov	x0, sp
	mov	x27, #0		// prevent syscall restart handling (why)
	b	compat_sys_rt_sigreturn
ENDPROC(compat_sys_rt_sigreturn_wrapper)

compat_sys_sigaltstack_wrapper:
	ldr	x2, [sp, #S_COMPAT_SP]
	b	compat_do_sigaltstack
ENDPROC(compat_sys_sigaltstack_wrapper)

compat_sys_statfs64_wrapper:
	mov	w3, #84
	cmp	w1, #88
	csel	w1, w3, w1, eq
	b	compat_sys_statfs64
ENDPROC(compat_sys_statfs64_wrapper)

compat_sys_fstatfs64_wrapper:
	mov	w3, #84
	cmp	w1, #88
	csel	w1, w3, w1, eq
	b	compat_sys_fstatfs64
ENDPROC(compat_sys_fstatfs64_wrapper)

/*
 * Wrappers for AArch32 syscalls that either take 64-bit parameters
 * in registers or that take 32-bit parameters which require sign
 * extension.
 */
compat_sys_lseek_wrapper:
	sxtw	x1, w1
	b	sys_lseek
ENDPROC(compat_sys_lseek_wrapper)

compat_sys_pread64_wrapper:
	orr	x3, x4, x5, lsl #32
	b	sys_pread64
ENDPROC(compat_sys_pread64_wrapper)

compat_sys_pwrite64_wrapper:
	orr	x3, x4, x5, lsl #32
	b	sys_pwrite64
ENDPROC(compat_sys_pwrite64_wrapper)

compat_sys_truncate64_wrapper:
	orr	x1, x2, x3, lsl #32
	b	sys_truncate
ENDPROC(compat_sys_truncate64_wrapper)

compat_sys_ftruncate64_wrapper:
	orr	x1, x2, x3, lsl #32
	b	sys_ftruncate
ENDPROC(compat_sys_ftruncate64_wrapper)

compat_sys_readahead_wrapper:
	orr	x1, x2, x3, lsl #32
	mov	w2, w4
	b	sys_readahead
ENDPROC(compat_sys_readahead_wrapper)

compat_sys_lookup_dcookie:
	orr	x0, x0, x1, lsl #32
	mov	w1, w2
	mov	w2, w3
	b	sys_lookup_dcookie
ENDPROC(compat_sys_lookup_dcookie)

compat_sys_fadvise64_64_wrapper:
	mov	w6, w1
	orr	x1, x2, x3, lsl #32
	orr	x2, x4, x5, lsl #32
	mov	w3, w6
	b	sys_fadvise64_64
ENDPROC(compat_sys_fadvise64_64_wrapper)

compat_sys_sync_file_range2_wrapper:
	orr	x2, x2, x3, lsl #32
	orr	x3, x4, x5, lsl #32
	b	sys_sync_file_range2
ENDPROC(compat_sys_sync_file_range2_wrapper)

compat_sys_fallocate_wrapper:
	orr	x2, x2, x3, lsl #32
	orr	x3, x4, x5, lsl #32
	b	sys_fallocate
ENDPROC(compat_sys_fallocate_wrapper)

compat_sys_fanotify_mark_wrapper:
	orr	x2, x2, x3, lsl #32
	mov	w3, w4
	mov	w4, w5
	b	sys_fanotify_mark
ENDPROC(compat_sys_fanotify_mark_wrapper)

/*
 * Use the compat system call wrappers.
 */
#define sys_fork		compat_sys_fork_wrapper
#define sys_open		compat_sys_open
#define sys_execve		compat_sys_execve_wrapper
#define sys_lseek		compat_sys_lseek_wrapper
#define sys_mount		compat_sys_mount
#define sys_ptrace		compat_sys_ptrace
#define sys_times		compat_sys_times
#define sys_ioctl		compat_sys_ioctl
#define sys_fcntl		compat_sys_fcntl
#define sys_ustat		compat_sys_ustat
#define sys_sigaction		compat_sys_sigaction
#define sys_sigsuspend		compat_sys_sigsuspend
#define sys_sigpending		compat_sys_sigpending
#define sys_setrlimit		compat_sys_setrlimit
#define sys_getrusage		compat_sys_getrusage
#define sys_gettimeofday	compat_sys_gettimeofday
#define sys_settimeofday	compat_sys_settimeofday
#define sys_statfs		compat_sys_statfs
#define sys_fstatfs		compat_sys_fstatfs
#define sys_setitimer		compat_sys_setitimer
#define sys_getitimer		compat_sys_getitimer
#define sys_newstat		compat_sys_newstat
#define sys_newlstat		compat_sys_newlstat
#define sys_newfstat		compat_sys_newfstat
#define sys_wait4		compat_sys_wait4
#define sys_sysinfo		compat_sys_sysinfo
#define sys_sigreturn		compat_sys_sigreturn_wrapper
#define sys_clone		compat_sys_clone_wrapper
#define sys_adjtimex		compat_sys_adjtimex
#define sys_sigprocmask		compat_sys_sigprocmask
#define sys_getdents		compat_sys_getdents
#define sys_select		compat_sys_select
#define sys_readv		compat_sys_readv
#define sys_writev		compat_sys_writev
#define sys_sysctl		compat_sys_sysctl
#define sys_sched_rr_get_interval compat_sys_sched_rr_get_interval
#define sys_nanosleep		compat_sys_nanosleep
#define sys_rt_sigreturn	compat_sys_rt_sigreturn_wrapper
#define sys_rt_sigaction	compat_sys_rt_sigaction
#define sys_rt_sigprocmask	compat_sys_rt_sigprocmask
#define sys_rt_sigpending	compat_sys_rt_sigpending
#define sys_rt_sigtimedwait	compat_sys_rt_sigtimedwait
#define sys_rt_sigqueueinfo	compat_sys_rt_sigqueueinfo
#define sys_rt_sigsuspend	compat_sys_rt_sigsuspend
#define sys_pread64		compat_sys_pread64_wrapper
#define sys_pwrite64		compat_sys_pwrite64_wrapper
#define sys_sigaltstack		compat_sys_sigaltstack_wrapper
#define sys_sendfile		compat_sys_sendfile
#define sys_vfork		compat_sys_vfork_wrapper
#define sys_getrlimit		compat_sys_getrlimit
#define sys_mmap2		sys_mmap_pgoff
#define sys_truncate64		compat_sys_truncate64_wrapper
#define sys_ftruncate64		compat_sys_ftruncate64_wrapper
#define sys_getdents64		compat_sys_getdents64
#define sys_fcntl64		compat_sys_fcntl64
#define sys_readahead		compat_sys_readahead_wrapper
#define sys_futex		compat_sys_futex
#define sys_sched_setaffinity	compat_sys_sched_setaffinity
#define sys_sched_getaffinity	compat_sys_sched_getaffinity
#define sys_io_setup		compat_sys_io_setup
#define sys_io_getevents	compat_sys_io_getevents
#define sys_io_submit		compat_sys_io_submit
#define sys_lookup_dcookie	compat_sys_lookup_dcookie
#define sys_timer_create	compat_sys_timer_create
#define sys_timer_settime	compat_sys_timer_settime
#define sys_timer_gettime	compat_sys_timer_gettime
#define sys_clock_settime	compat_sys_clock_settime
#define sys_clock_gettime	compat_sys_clock_gettime
#define sys_clock_getres	compat_sys_clock_getres
#define sys_clock_nanosleep	compat_sys_clock_nanosleep
#define sys_statfs64		compat_sys_statfs64_wrapper
#define sys_fstatfs64		compat_sys_fstatfs64_wrapper
#define sys_utimes		compat_sys_utimes
#define sys_fadvise64_64	compat_sys_fadvise64_64_wrapper
#define sys_mq_open		compat_sys_mq_open
#define sys_mq_timedsend	compat_sys_mq_timedsend
#define sys_mq_timedreceive	compat_sys_mq_timedreceive
#define sys_mq_notify		compat_sys_mq_notify
#define sys_mq_getsetattr	compat_sys_mq_getsetattr
#define sys_waitid		compat_sys_waitid
#define sys_recv		compat_sys_recv
#define sys_recvfrom		compat_sys_recvfrom
#define sys_setsockopt		compat_sys_setsockopt
#define sys_getsockopt		compat_sys_getsockopt
#define sys_sendmsg		compat_sys_sendmsg
#define sys_recvmsg		compat_sys_recvmsg
#define sys_semctl		compat_sys_semctl
#define sys_msgsnd		compat_sys_msgsnd
#define sys_msgrcv		compat_sys_msgrcv
#define sys_msgctl		compat_sys_msgctl
#define sys_shmat		compat_sys_shmat
#define sys_shmctl		compat_sys_shmctl
#define sys_keyctl		compat_sys_keyctl
#define sys_semtimedop		compat_sys_semtimedop
#define sys_mbind		compat_sys_mbind
#define sys_get_mempolicy	compat_sys_get_mempolicy
#define sys_set_mempolicy	compat_sys_set_mempolicy
#define sys_openat		compat_sys_openat
#define sys_futimesat		compat_sys_futimesat
#define sys_pselect6		compat_sys_pselect6
#define sys_ppoll		compat_sys_ppoll
#define sys_set_robust_list	compat_sys_set_robust_list
#define sys_get_robust_list	compat_sys_get_robust_list
#define sys_sync_file_range2	compat_sys_sync_file_range2_wrapper
#define sys_vmsplice		compat_sys_vmsplice
#define sys_move_pages		compat_sys_move_pages
#define sys_epoll_pwait		compat_sys_epoll_pwait
#define sys_kexec_load		compat_sys_kexec_load
#define sys_utimensat		compat_sys_utimensat
#define sys_signalfd		compat_sys_signalfd
#define sys_fallocate		compat_sys_fallocate_wrapper
#define sys_timerfd_settime	compat_sys_timerfd_settime
#define sys_timerfd_gettime	compat_sys_timerfd_gettime
#define sys_signalfd4		compat_sys_signalfd4
#define sys_preadv		compat_sys_preadv
#define sys_pwritev		compat_sys_pwritev
#define sys_rt_tgsigqueueinfo	compat_sys_rt_tgsigqueueinfo
#define sys_recvmmsg		compat_sys_recvmmsg
#define sys_fanotify_mark	compat_sys_fanotify_mark_wrapper

#undef __SYSCALL
#define __SYSCALL(x, y)		.quad	y	// x
#define __SYSCALL_COMPAT

/*
 * The system calls table must be 4KB aligned.
 */
	.align	12
ENTRY(compat_sys_call_table)
#include <asm/unistd.h>
