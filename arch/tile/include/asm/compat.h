/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_COMPAT_H
#define _ASM_TILE_COMPAT_H

/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>

#define COMPAT_USER_HZ	100

/* "long" and pointer-based types are different. */
typedef s32		compat_long_t;
typedef u32		compat_ulong_t;
typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_off_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef u32		compat_ino_t;
typedef u32		compat_caddr_t;
typedef	u32		compat_uptr_t;

/* Many types are "int" or otherwise the same. */
typedef __kernel_pid_t compat_pid_t;
typedef __kernel_uid_t __compat_uid_t;
typedef __kernel_gid_t __compat_gid_t;
typedef __kernel_uid32_t __compat_uid32_t;
typedef __kernel_uid32_t __compat_gid32_t;
typedef __kernel_mode_t compat_mode_t;
typedef __kernel_dev_t compat_dev_t;
typedef __kernel_loff_t compat_loff_t;
typedef __kernel_ipc_pid_t compat_ipc_pid_t;
typedef __kernel_daddr_t compat_daddr_t;
typedef __kernel_fsid_t	compat_fsid_t;
typedef __kernel_timer_t compat_timer_t;
typedef __kernel_key_t compat_key_t;
typedef int compat_int_t;
typedef s64 compat_s64;
typedef uint compat_uint_t;
typedef u64 compat_u64;

/* We use the same register dump format in 32-bit images. */
typedef unsigned long compat_elf_greg_t;
#define COMPAT_ELF_NGREG (sizeof(struct pt_regs) / sizeof(compat_elf_greg_t))
typedef compat_elf_greg_t compat_elf_gregset_t[COMPAT_ELF_NGREG];

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

#define compat_stat stat
#define compat_statfs statfs

struct compat_sysctl {
	unsigned int	name;
	int		nlen;
	unsigned int	oldval;
	unsigned int	oldlenp;
	unsigned int	newval;
	unsigned int	newlen;
	unsigned int	__unused[4];
};


struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	compat_pid_t	l_pid;
};

#define F_GETLK64	12	/*  using 'struct flock64' */
#define F_SETLK64	13
#define F_SETLKW64	14

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
};

#define COMPAT_RLIM_INFINITY		0xffffffff

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32               compat_sigset_word;

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

#define COMPAT_SI_PAD_SIZE	(128/sizeof(int) - 3)

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[COMPAT_SI_PAD_SIZE];

		/* kill() */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;	/* not to be passed to user */
			int _overrun_incr;	/* amount to add to overrun */
		} _timer;

		/* POSIX.1b signals */
		struct {
			unsigned int _pid;	/* sender's pid */
			unsigned int _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			unsigned int _pid;	/* which child */
			unsigned int _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			unsigned int _addr;	/* faulting insn/memory ref. */
#ifdef __ARCH_SI_TRAPNO
			int _trapno;	/* TRAP # which caused the signal */
#endif
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

#define COMPAT_OFF_T_MAX	0x7fffffff

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	unsigned short mode;
	unsigned short __pad1;
	unsigned short seq;
	unsigned short __pad2;
	compat_ulong_t unused1;
	compat_ulong_t unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_time_t  sem_otime;
	compat_ulong_t __unused1;
	compat_time_t  sem_ctime;
	compat_ulong_t __unused2;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	compat_time_t  msg_stime;
	compat_ulong_t __unused1;
	compat_time_t  msg_rtime;
	compat_ulong_t __unused2;
	compat_time_t  msg_ctime;
	compat_ulong_t __unused3;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t   msg_lspid;
	compat_pid_t   msg_lrpid;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t  shm_segsz;
	compat_time_t  shm_atime;
	compat_ulong_t __unused1;
	compat_time_t  shm_dtime;
	compat_ulong_t __unused2;
	compat_time_t  shm_ctime;
	compat_ulong_t __unused3;
	compat_pid_t   shm_cpid;
	compat_pid_t   shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately converted them already.
 */

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(long)(s32)uptr;
}

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

/* Sign-extend when storing a kernel pointer to a user's ptregs. */
static inline unsigned long ptr_to_compat_reg(void __user *uptr)
{
	return (long)(int)(long __force)uptr;
}

static inline void __user *arch_compat_alloc_user_space(long len)
{
	struct pt_regs *regs = task_pt_regs(current);
	return (void __user *)regs->sp - len;
}

static inline int is_compat_task(void)
{
	return current_thread_info()->status & TS_COMPAT;
}

extern int compat_setup_rt_frame(struct ksignal *ksig, sigset_t *set,
				 struct pt_regs *regs);

/* Compat syscalls. */
struct compat_siginfo;
struct compat_sigaltstack;
long compat_sys_rt_sigreturn(void);
long compat_sys_truncate64(char __user *filename, u32 dummy, u32 low, u32 high);
long compat_sys_ftruncate64(unsigned int fd, u32 dummy, u32 low, u32 high);
long compat_sys_pread64(unsigned int fd, char __user *ubuf, size_t count,
			u32 dummy, u32 low, u32 high);
long compat_sys_pwrite64(unsigned int fd, char __user *ubuf, size_t count,
			 u32 dummy, u32 low, u32 high);
long compat_sys_sync_file_range2(int fd, unsigned int flags,
				 u32 offset_lo, u32 offset_hi,
				 u32 nbytes_lo, u32 nbytes_hi);
long compat_sys_fallocate(int fd, int mode,
			  u32 offset_lo, u32 offset_hi,
			  u32 len_lo, u32 len_hi);
long compat_sys_llseek(unsigned int fd, unsigned int offset_high,
		       unsigned int offset_low, loff_t __user * result,
		       unsigned int origin);

/* Assembly trampoline to avoid clobbering r0. */
long _compat_sys_rt_sigreturn(void);

#endif /* _ASM_TILE_COMPAT_H */
