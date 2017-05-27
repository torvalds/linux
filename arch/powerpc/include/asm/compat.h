#ifndef _ASM_POWERPC_COMPAT_H
#define _ASM_POWERPC_COMPAT_H
#ifdef __KERNEL__
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>

#define COMPAT_USER_HZ		100
#ifdef __BIG_ENDIAN__
#define COMPAT_UTS_MACHINE	"ppc\0\0"
#else
#define COMPAT_UTS_MACHINE	"ppcle\0\0"
#endif

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_pid_t;
typedef u32		__compat_uid_t;
typedef u32		__compat_gid_t;
typedef u32		__compat_uid32_t;
typedef u32		__compat_gid32_t;
typedef u32		compat_mode_t;
typedef u32		compat_ino_t;
typedef u32		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef s16		compat_nlink_t;
typedef u16		compat_ipc_pid_t;
typedef s32		compat_daddr_t;
typedef u32		compat_caddr_t;
typedef __kernel_fsid_t	compat_fsid_t;
typedef s32		compat_key_t;
typedef s32		compat_timer_t;

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
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid32_t	st_uid;
	__compat_gid32_t	st_gid;
	compat_dev_t	st_rdev;
	compat_off_t	st_size;
	compat_off_t	st_blksize;
	compat_off_t	st_blocks;
	compat_time_t	st_atime;
	u32		st_atime_nsec;
	compat_time_t	st_mtime;
	u32		st_mtime_nsec;
	compat_time_t	st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4[2];
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

struct compat_statfs {
	int		f_type;
	int		f_bsize;
	int		f_blocks;
	int		f_bfree;
	int		f_bavail;
	int		f_files;
	int		f_ffree;
	compat_fsid_t	f_fsid;
	int		f_namelen;	/* SunOS ignores this field. */
	int		f_frsize;
	int		f_flags;
	int		f_spare[4];
};

#define COMPAT_RLIM_INFINITY		0xffffffff

typedef u32		compat_old_sigset_t;

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

#define SI_PAD_SIZE32	(128/sizeof(int) - 3)

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[SI_PAD_SIZE32];

		/* kill() */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			__compat_uid_t _uid;		/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;		/* timer id */
			int _overrun;			/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;	/* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			compat_pid_t _pid;		/* sender's pid */
			__compat_uid_t _uid;		/* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			compat_pid_t _pid;		/* which child */
			__compat_uid_t _uid;		/* sender's uid */
			int _status;			/* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS, SIGEMT */
		struct {
			unsigned int _addr; /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		/* SIGSYS */
		struct {
			unsigned int _call_addr; /* calling insn */
			int _syscall;		 /* triggering system call number */
			unsigned int _arch;	 /* AUDIT_ARCH_* of syscall */
		} _sigsys;
	} _sifields;
} compat_siginfo_t;

#define COMPAT_OFF_T_MAX	0x7fffffff
#define COMPAT_LOFF_T_MAX	0x7fffffffffffffffL

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately converted them already.
 */

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)uptr;
}

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

static inline void __user *arch_compat_alloc_user_space(long len)
{
	struct pt_regs *regs = current->thread.regs;
	unsigned long usp = regs->gpr[1];

	/*
	 * We can't access below the stack pointer in the 32bit ABI and
	 * can access 288 bytes in the 64bit big-endian ABI,
	 * or 512 bytes with the new ELFv2 little-endian ABI.
	 */
	if (!is_32bit_task())
		usp -= USER_REDZONE_SIZE;

	return (void __user *) (usp - len);
}

/*
 * ipc64_perm is actually 32/64bit clean but since the compat layer refers to
 * it we may as well define it.
 */
struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid_t uid;
	__compat_gid_t gid;
	__compat_uid_t cuid;
	__compat_gid_t cgid;
	compat_mode_t mode;
	unsigned int seq;
	unsigned int __pad2;
	unsigned long __unused1;	/* yes they really are 64bit pads */
	unsigned long __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	unsigned int __unused1;
	compat_time_t sem_otime;
	unsigned int __unused2;
	compat_time_t sem_ctime;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	unsigned int __unused1;
	compat_time_t msg_stime;
	unsigned int __unused2;
	compat_time_t msg_rtime;
	unsigned int __unused3;
	compat_time_t msg_ctime;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t msg_lspid;
	compat_pid_t msg_lrpid;
	compat_ulong_t __unused4;
	compat_ulong_t __unused5;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	unsigned int __unused1;
	compat_time_t shm_atime;
	unsigned int __unused2;
	compat_time_t shm_dtime;
	unsigned int __unused3;
	compat_time_t shm_ctime;
	unsigned int __unused4;
	compat_size_t shm_segsz;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused5;
	compat_ulong_t __unused6;
};

static inline int is_compat_task(void)
{
	return is_32bit_task();
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_COMPAT_H */
