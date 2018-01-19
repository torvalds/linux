/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_COMPAT_H
#define _ASM_PARISC_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#define COMPAT_USER_HZ 		100
#define COMPAT_UTS_MACHINE	"parisc\0\0"

typedef u32	compat_size_t;
typedef s32	compat_ssize_t;
typedef s32	compat_time_t;
typedef s32	compat_clock_t;
typedef s32	compat_pid_t;
typedef u32	__compat_uid_t;
typedef u32	__compat_gid_t;
typedef u32	__compat_uid32_t;
typedef u32	__compat_gid32_t;
typedef u16	compat_mode_t;
typedef u32	compat_ino_t;
typedef u32	compat_dev_t;
typedef s32	compat_off_t;
typedef s64	compat_loff_t;
typedef u16	compat_nlink_t;
typedef u16	compat_ipc_pid_t;
typedef s32	compat_daddr_t;
typedef u32	compat_caddr_t;
typedef s32	compat_key_t;
typedef s32	compat_timer_t;

typedef s32	compat_int_t;
typedef s32	compat_long_t;
typedef s64	compat_s64;
typedef u32	compat_uint_t;
typedef u32	compat_ulong_t;
typedef u64	compat_u64;
typedef u32	compat_uptr_t;

struct compat_timespec {
	compat_time_t		tv_sec;
	s32			tv_nsec;
};

struct compat_timeval {
	compat_time_t		tv_sec;
	s32			tv_usec;
};

struct compat_stat {
	compat_dev_t		st_dev;	/* dev_t is 32 bits on parisc */
	compat_ino_t		st_ino;	/* 32 bits */
	compat_mode_t		st_mode;	/* 16 bits */
	compat_nlink_t  	st_nlink;	/* 16 bits */
	u16			st_reserved1;	/* old st_uid */
	u16			st_reserved2;	/* old st_gid */
	compat_dev_t		st_rdev;
	compat_off_t		st_size;
	compat_time_t		st_atime;
	u32			st_atime_nsec;
	compat_time_t		st_mtime;
	u32			st_mtime_nsec;
	compat_time_t		st_ctime;
	u32			st_ctime_nsec;
	s32			st_blksize;
	s32			st_blocks;
	u32			__unused1;	/* ACL stuff */
	compat_dev_t		__unused2;	/* network */
	compat_ino_t		__unused3;	/* network */
	u32			__unused4;	/* cnodes */
	u16			__unused5;	/* netsite */
	short			st_fstype;
	compat_dev_t		st_realdev;
	u16			st_basemode;
	u16			st_spareshort;
	__compat_uid32_t	st_uid;
	__compat_gid32_t	st_gid;
	u32			st_spare4[3];
};

struct compat_flock {
	short			l_type;
	short			l_whence;
	compat_off_t		l_start;
	compat_off_t		l_len;
	compat_pid_t		l_pid;
};

struct compat_flock64 {
	short			l_type;
	short			l_whence;
	compat_loff_t		l_start;
	compat_loff_t		l_len;
	compat_pid_t		l_pid;
};

struct compat_statfs {
	s32		f_type;
	s32		f_bsize;
	s32		f_blocks;
	s32		f_bfree;
	s32		f_bavail;
	s32		f_files;
	s32		f_ffree;
	__kernel_fsid_t	f_fsid;
	s32		f_namelen;
	s32		f_frsize;
	s32		f_flags;
	s32		f_spare[4];
};

struct compat_sigcontext {
	compat_int_t sc_flags;
	compat_int_t sc_gr[32]; /* PSW in sc_gr[0] */
	u64 sc_fr[32];
	compat_int_t sc_iasq[2];
	compat_int_t sc_iaoq[2];
	compat_int_t sc_sar; /* cr11 */
};

#define COMPAT_RLIM_INFINITY 0xffffffff

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

typedef union compat_sigval {
	compat_int_t	sival_int;
	compat_uptr_t	sival_ptr;
} compat_sigval_t;

typedef struct compat_siginfo {
	int si_signo;
	int si_errno;
	int si_code;

	union {
		int _pad[128/sizeof(int) - 3];

		/* kill() */
		struct {
			unsigned int _pid;      /* sender's pid */
			unsigned int _uid;      /* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;            /* timer id */
			int _overrun;           /* overrun count */
			char _pad[sizeof(unsigned int) - sizeof(int)];
			compat_sigval_t _sigval;        /* same as below */
			int _sys_private;       /* not to be passed to user */
		} _timer;

		/* POSIX.1b signals */
		struct {
			unsigned int _pid;      /* sender's pid */
			unsigned int _uid;      /* sender's uid */
			compat_sigval_t _sigval;
		} _rt;

		/* SIGCHLD */
		struct {
			unsigned int _pid;      /* which child */
			unsigned int _uid;      /* sender's uid */
			int _status;            /* exit code */
			compat_clock_t _utime;
			compat_clock_t _stime;
		} _sigchld;

		/* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
		struct {
			unsigned int _addr;     /* faulting insn/memory ref. */
		} _sigfault;

		/* SIGPOLL */
		struct {
			int _band;      /* POLL_IN, POLL_OUT, POLL_MSG */
			int _fd;
		} _sigpoll;

		/* SIGSYS */
		struct {
			compat_uptr_t _call_addr; /* calling user insn */
			int _syscall;	/* triggering system call number */
			compat_uint_t _arch;	/* AUDIT_ARCH_* of syscall */
		} _sigsys;
	} _sifields;
} compat_siginfo_t;

#define COMPAT_OFF_T_MAX	0x7fffffff
#define COMPAT_LOFF_T_MAX	0x7fffffffffffffffL

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid_t uid;
	__compat_gid_t gid;
	__compat_uid_t cuid;
	__compat_gid_t cgid;
	unsigned short int __pad1;
	compat_mode_t mode;
	unsigned short int __pad2;
	unsigned short int seq;
	unsigned int __pad3;
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

static __inline__ void __user *arch_compat_alloc_user_space(long len)
{
	struct pt_regs *regs = &current->thread.regs;
	return (void __user *)regs->gr[30];
}

static inline int __is_compat_task(struct task_struct *t)
{
	return test_ti_thread_flag(task_thread_info(t), TIF_32BIT);
}

static inline int is_compat_task(void)
{
	return __is_compat_task(current);
}

#endif /* _ASM_PARISC_COMPAT_H */
