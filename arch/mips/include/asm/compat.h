/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_COMPAT_H
#define _ASM_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/thread_info.h>
#include <linux/types.h>
#include <asm/page.h>
#include <asm/ptrace.h>

#define COMPAT_USER_HZ		100
#define COMPAT_UTS_MACHINE	"mips\0\0\0"

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_suseconds_t;

typedef s32		compat_pid_t;
typedef s32		__compat_uid_t;
typedef s32		__compat_gid_t;
typedef __compat_uid_t	__compat_uid32_t;
typedef __compat_gid_t	__compat_gid32_t;
typedef u32		compat_mode_t;
typedef u32		compat_ino_t;
typedef u32		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef u32		compat_nlink_t;
typedef s32		compat_ipc_pid_t;
typedef s32		compat_daddr_t;
typedef s32		compat_caddr_t;
typedef struct {
	s32	val[2];
} compat_fsid_t;
typedef s32		compat_timer_t;
typedef s32		compat_key_t;

typedef s32		compat_int_t;
typedef s32		compat_long_t;
typedef s64		compat_s64;
typedef u32		compat_uint_t;
typedef u32		compat_ulong_t;
typedef u64		compat_u64;
typedef u32		compat_uptr_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

struct compat_stat {
	compat_dev_t	st_dev;
	s32		st_pad1[3];
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	s32		st_pad2[2];
	compat_off_t	st_size;
	s32		st_pad3;
	compat_time_t	st_atime;
	s32		st_atime_nsec;
	compat_time_t	st_mtime;
	s32		st_mtime_nsec;
	compat_time_t	st_ctime;
	s32		st_ctime_nsec;
	s32		st_blksize;
	s32		st_blocks;
	s32		st_pad4[14];
};

struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	s32		l_sysid;
	compat_pid_t	l_pid;
	s32		pad[4];
};

#define F_GETLK64	33
#define F_SETLK64	34
#define F_SETLKW64	35

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
};

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_frsize;
	int		f_blocks;
	int		f_bfree;
	int		f_files;
	int		f_ffree;
	int		f_bavail;
	compat_fsid_t	f_fsid;
	int		f_namelen;
	int		f_flags;
	int		f_spare[5];
};

#define COMPAT_RLIM_INFINITY	0x7fffffffUL

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		128		/* Don't ask !$@#% ...	*/
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

/* Can't use the generic version because si_code and si_errno are swapped */

#define SI_PAD_SIZE32	(128/sizeof(int) - 3)

typedef struct compat_siginfo {
	int si_signo;
	int si_code;
	int si_errno;

	union {
		int _pad[128 / sizeof(int) - 3];

		/* kill() */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			__compat_uid32_t _uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;	/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;	/* sender's pid */
			__compat_uid32_t _uid;	/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;	/* which child */
			__compat_uid32_t _uid;	/* sender's uid */
			int _status;		/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			compat_uptr_t _addr;	/* faulting insn/memory ref. */
#ifdef __ARCH_SI_TRAPNO
			int _trapno;	/* TRAP # which caused the signal */
#endif
			short _addr_lsb; /* LSB of the reported address */
			struct {
				compat_uptr_t _lower;
				compat_uptr_t _upper;
			} _addr_bnd;
		} _sigfault;

		/* SIGPOLL */
		struct {
			compat_long_t _band; /* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		struct {
			compat_uptr_t _call_addr; /* calling insn */
			int _syscall;	/* triggering system call number */
			compat_uint_t _arch;	/* AUDIT_ARCH_* of syscall */
		} _sigsys;
	} _sifields;
} compat_siginfo_t;

#define COMPAT_OFF_T_MAX	0x7fffffff

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately converted them already.
 */

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	/* cast to a __user pointer via "unsigned long" makes sparse happy */
	return (void __user *)(unsigned long)(long)uptr;
}

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

static inline void __user *arch_compat_alloc_user_space(long len)
{
	struct pt_regs *regs = (struct pt_regs *)
		((unsigned long) current_thread_info() + THREAD_SIZE - 32) - 1;

	return (void __user *) (regs->regs[29] - len);
}

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	compat_mode_t mode;
	unsigned short seq;
	unsigned short __pad2;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_time_t	sem_otime;
	compat_time_t	sem_ctime;
	compat_ulong_t	sem_nsems;
	compat_ulong_t	__unused1;
	compat_ulong_t	__unused2;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused1;
#endif
	compat_time_t	msg_stime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused1;
#endif
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused2;
#endif
	compat_time_t	msg_rtime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused2;
#endif
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused3;
#endif
	compat_time_t	msg_ctime;
#ifdef CONFIG_CPU_LITTLE_ENDIAN
	compat_ulong_t	__unused3;
#endif
	compat_ulong_t	msg_cbytes;
	compat_ulong_t	msg_qnum;
	compat_ulong_t	msg_qbytes;
	compat_pid_t	msg_lspid;
	compat_pid_t	msg_lrpid;
	compat_ulong_t	__unused4;
	compat_ulong_t	__unused5;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t	shm_segsz;
	compat_time_t	shm_atime;
	compat_time_t	shm_dtime;
	compat_time_t	shm_ctime;
	compat_pid_t	shm_cpid;
	compat_pid_t	shm_lpid;
	compat_ulong_t	shm_nattch;
	compat_ulong_t	__unused1;
	compat_ulong_t	__unused2;
};

/* MIPS has unusual order of fields in stack_t */
typedef struct compat_sigaltstack {
	compat_uptr_t			ss_sp;
	compat_size_t			ss_size;
	int				ss_flags;
} compat_stack_t;
#define compat_sigaltstack compat_sigaltstack

static inline int is_compat_task(void)
{
	return test_thread_flag(TIF_32BIT_ADDR);
}

#endif /* _ASM_COMPAT_H */
