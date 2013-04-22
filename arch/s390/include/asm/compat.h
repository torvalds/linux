#ifndef _ASM_S390X_COMPAT_H
#define _ASM_S390X_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#define __TYPE_IS_PTR(t) (!__builtin_types_compatible_p(typeof(0?(t)0:0ULL), u64))
#define __SC_DELOUSE(t,v) (t)(__TYPE_IS_PTR(t) ? ((v) & 0x7fffffff) : (v))

#define PSW32_MASK_PER		0x40000000UL
#define PSW32_MASK_DAT		0x04000000UL
#define PSW32_MASK_IO		0x02000000UL
#define PSW32_MASK_EXT		0x01000000UL
#define PSW32_MASK_KEY		0x00F00000UL
#define PSW32_MASK_BASE		0x00080000UL	/* Always one */
#define PSW32_MASK_MCHECK	0x00040000UL
#define PSW32_MASK_WAIT		0x00020000UL
#define PSW32_MASK_PSTATE	0x00010000UL
#define PSW32_MASK_ASC		0x0000C000UL
#define PSW32_MASK_CC		0x00003000UL
#define PSW32_MASK_PM		0x00000f00UL

#define PSW32_MASK_USER		0x0000FF00UL

#define PSW32_ADDR_AMODE	0x80000000UL
#define PSW32_ADDR_INSN		0x7FFFFFFFUL

#define PSW32_DEFAULT_KEY	(((u32) PAGE_DEFAULT_ACC) << 20)

#define PSW32_ASC_PRIMARY	0x00000000UL
#define PSW32_ASC_ACCREG	0x00004000UL
#define PSW32_ASC_SECONDARY	0x00008000UL
#define PSW32_ASC_HOME		0x0000C000UL

extern u32 psw32_user_bits;

#define COMPAT_USER_HZ		100
#define COMPAT_UTS_MACHINE	"s390\0\0\0\0"

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_pid_t;
typedef u16		__compat_uid_t;
typedef u16		__compat_gid_t;
typedef u32		__compat_uid32_t;
typedef u32		__compat_gid32_t;
typedef u16		compat_mode_t;
typedef u32		compat_ino_t;
typedef u16		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef u16		compat_nlink_t;
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

typedef struct {
	u32 mask;
	u32 addr;
} __aligned(8) psw_compat_t;

typedef struct {
	psw_compat_t psw;
	u32 gprs[NUM_GPRS];
	u32 acrs[NUM_ACRS];
	u32 orig_gpr2;
} s390_compat_regs;

typedef struct {
	u32 gprs_high[NUM_GPRS];
} s390_compat_regs_high;

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
	u16		__pad1;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	u16		__pad2;
	u32		st_size;
	u32		st_blksize;
	u32		st_blocks;
	u32		st_atime;
	u32		st_atime_nsec;
	u32		st_mtime;
	u32		st_mtime_nsec;
	u32		st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4;
	u32		__unused5;
};

struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	compat_pid_t	l_pid;
};

#define F_GETLK64       12
#define F_SETLK64       13
#define F_SETLKW64      14    

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
};

struct compat_statfs {
	u32		f_type;
	u32		f_bsize;
	u32		f_blocks;
	u32		f_bfree;
	u32		f_bavail;
	u32		f_files;
	u32		f_ffree;
	compat_fsid_t	f_fsid;
	u32		f_namelen;
	u32		f_frsize;
	u32		f_flags;
	u32		f_spare[4];
};

struct compat_statfs64 {
	u32		f_type;
	u32		f_bsize;
	u64		f_blocks;
	u64		f_bfree;
	u64		f_bavail;
	u64		f_files;
	u64		f_ffree;
	compat_fsid_t	f_fsid;
	u32		f_namelen;
	u32		f_frsize;
	u32		f_flags;
	u32		f_spare[4];
};

#define COMPAT_RLIM_OLD_INFINITY	0x7fffffff
#define COMPAT_RLIM_INFINITY		0xffffffff

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

typedef struct compat_siginfo {
	int	si_signo;
	int	si_errno;
	int	si_code;

	union {
		int _pad[128/sizeof(int) - 3];

		/* kill() */
		struct {
			pid_t	_pid;	/* sender's pid */
			uid_t	_uid;	/* sender's uid */
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
			pid_t			_pid;	/* sender's pid */
			uid_t			_uid;	/* sender's uid */
			compat_sigval_t		_sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			pid_t			_pid;	/* which child */
			uid_t			_uid;	/* sender's uid */
			int			_status;/* exit code */
			compat_clock_t		_utime;
			compat_clock_t		_stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			__u32	_addr;	/* faulting insn/memory ref. - pointer */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int	_band;	/* POLL_IN, POLL_OUT, POLL_MSG */
			int	_fd;
		} _sigpoll;
	} _sifields;
} compat_siginfo_t;

/*
 * How these fields are to be accessed.
 */
#define si_pid		_sifields._kill._pid
#define si_uid		_sifields._kill._uid
#define si_status	_sifields._sigchld._status
#define si_utime	_sifields._sigchld._utime
#define si_stime	_sifields._sigchld._stime
#define si_value	_sifields._rt._sigval
#define si_int		_sifields._rt._sigval.sival_int
#define si_ptr		_sifields._rt._sigval.sival_ptr
#define si_addr		_sifields._sigfault._addr
#define si_band		_sifields._sigpoll._band
#define si_fd		_sifields._sigpoll._fd
#define si_tid		_sifields._timer._tid
#define si_overrun	_sifields._timer._overrun

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
	return (void __user *)(unsigned long)(uptr & 0x7fffffffUL);
}

static inline compat_uptr_t ptr_to_compat(void __user *uptr)
{
	return (u32)(unsigned long)uptr;
}

#ifdef CONFIG_COMPAT

static inline int is_compat_task(void)
{
	return is_32bit_task();
}

static inline void __user *arch_compat_alloc_user_space(long len)
{
	unsigned long stack;

	stack = KSTK_ESP(current);
	if (is_compat_task())
		stack &= 0x7fffffffUL;
	return (void __user *) (stack - len);
}

#endif

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	compat_mode_t mode;
	unsigned short __pad1;
	unsigned short seq;
	unsigned short __pad2;
	unsigned int __unused1;
	unsigned int __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_time_t  sem_otime;
	compat_ulong_t __pad1;
	compat_time_t  sem_ctime;
	compat_ulong_t __pad2;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	compat_time_t   msg_stime;
	compat_ulong_t __pad1;
	compat_time_t   msg_rtime;
	compat_ulong_t __pad2;
	compat_time_t   msg_ctime;
	compat_ulong_t __pad3;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t   msg_lspid;
	compat_pid_t   msg_lrpid;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t  shm_segsz;
	compat_time_t  shm_atime;
	compat_ulong_t __pad1;
	compat_time_t  shm_dtime;
	compat_ulong_t __pad2;
	compat_time_t  shm_ctime;
	compat_ulong_t __pad3;
	compat_pid_t   shm_cpid;
	compat_pid_t   shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};
#endif /* _ASM_S390X_COMPAT_H */
