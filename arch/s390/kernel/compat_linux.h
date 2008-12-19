#ifndef _ASM_S390X_S390_H
#define _ASM_S390X_S390_H

#include <linux/compat.h>
#include <linux/socket.h>
#include <linux/syscalls.h>
#include <linux/nfs_fs.h>
#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/export.h>

/* Macro that masks the high order bit of an 32 bit pointer and converts it*/
/*       to a 64 bit pointer */
#define A(__x) ((unsigned long)((__x) & 0x7FFFFFFFUL))
#define AA(__x)				\
	((unsigned long)(__x))

/* Now 32bit compatibility types */
struct ipc_kludge_32 {
        __u32   msgp;                           /* pointer              */
        __s32   msgtyp;
};

struct old_sigaction32 {
       __u32			sa_handler;	/* Really a pointer, but need to deal with 32 bits */
       compat_old_sigset_t	sa_mask;	/* A 32 bit mask */
       __u32			sa_flags;
       __u32			sa_restorer;	/* Another 32 bit pointer */
};
 
typedef struct compat_siginfo {
	int	si_signo;
	int	si_errno;
	int	si_code;

	union {
		int _pad[((128/sizeof(int)) - 3)];

		/* kill() */
		struct {
			pid_t	_pid;	/* sender's pid */
			uid_t	_uid;	/* sender's uid */
		} _kill;

		/* POSIX.1b timers */
		struct {
			compat_timer_t _tid;		/* timer id */
			int _overrun;		/* overrun count */
			compat_sigval_t _sigval;	/* same as below */
			int _sys_private;       /* not to be passed to user */
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

/* asm/sigcontext.h */
typedef union
{
	__u64   d;
	__u32   f; 
} freg_t32;

typedef struct
{
	unsigned int	fpc;
	freg_t32	fprs[__NUM_FPRS];              
} _s390_fp_regs32;

typedef struct 
{
        __u32   mask;
        __u32	addr;
} _psw_t32 __attribute__ ((aligned(8)));

typedef struct
{
	_psw_t32	psw;
	__u32		gprs[__NUM_GPRS];
	__u32		acrs[__NUM_ACRS];
} _s390_regs_common32;

typedef struct
{
	_s390_regs_common32 regs;
	_s390_fp_regs32     fpregs;
} _sigregs32;

#define _SIGCONTEXT_NSIG32	64
#define _SIGCONTEXT_NSIG_BPW32	32
#define __SIGNAL_FRAMESIZE32	96
#define _SIGMASK_COPY_SIZE32	(sizeof(u32)*2)

struct sigcontext32
{
	__u32	oldmask[_COMPAT_NSIG_WORDS];
	__u32	sregs;				/* pointer */
};

/* asm/signal.h */
struct sigaction32 {
	__u32		sa_handler;		/* pointer */
	__u32		sa_flags;
        __u32		sa_restorer;		/* pointer */
	compat_sigset_t	sa_mask;        /* mask last for extensibility */
};

typedef struct {
	__u32			ss_sp;		/* pointer */
	int			ss_flags;
	compat_size_t		ss_size;
} stack_t32;

/* asm/ucontext.h */
struct ucontext32 {
	__u32			uc_flags;
	__u32			uc_link;	/* pointer */	
	stack_t32		uc_stack;
	_sigregs32		uc_mcontext;
	compat_sigset_t		uc_sigmask;	/* mask last for extensibility */
};

struct __sysctl_args32;
struct stat64_emu31;
struct mmap_arg_struct_emu31;
struct fadvise64_64_args;
struct old_sigaction32;
struct old_sigaction32;

long sys32_chown16(const char __user * filename, u16 user, u16 group);
long sys32_lchown16(const char __user * filename, u16 user, u16 group);
long sys32_fchown16(unsigned int fd, u16 user, u16 group);
long sys32_setregid16(u16 rgid, u16 egid);
long sys32_setgid16(u16 gid);
long sys32_setreuid16(u16 ruid, u16 euid);
long sys32_setuid16(u16 uid);
long sys32_setresuid16(u16 ruid, u16 euid, u16 suid);
long sys32_getresuid16(u16 __user *ruid, u16 __user *euid, u16 __user *suid);
long sys32_setresgid16(u16 rgid, u16 egid, u16 sgid);
long sys32_getresgid16(u16 __user *rgid, u16 __user *egid, u16 __user *sgid);
long sys32_setfsuid16(u16 uid);
long sys32_setfsgid16(u16 gid);
long sys32_getgroups16(int gidsetsize, u16 __user *grouplist);
long sys32_setgroups16(int gidsetsize, u16 __user *grouplist);
long sys32_getuid16(void);
long sys32_geteuid16(void);
long sys32_getgid16(void);
long sys32_getegid16(void);
long sys32_ipc(u32 call, int first, int second, int third, u32 ptr);
long sys32_truncate64(const char __user * path, unsigned long high,
		      unsigned long low);
long sys32_ftruncate64(unsigned int fd, unsigned long high, unsigned long low);
long sys32_sched_rr_get_interval(compat_pid_t pid,
				 struct compat_timespec __user *interval);
long sys32_rt_sigprocmask(int how, compat_sigset_t __user *set,
			  compat_sigset_t __user *oset, size_t sigsetsize);
long sys32_rt_sigpending(compat_sigset_t __user *set, size_t sigsetsize);
long sys32_rt_sigqueueinfo(int pid, int sig, compat_siginfo_t __user *uinfo);
long sys32_execve(void);
long sys32_init_module(void __user *umod, unsigned long len,
		       const char __user *uargs);
long sys32_delete_module(const char __user *name_user, unsigned int flags);
long sys32_pread64(unsigned int fd, char __user *ubuf, size_t count,
		   u32 poshi, u32 poslo);
long sys32_pwrite64(unsigned int fd, const char __user *ubuf,
		    size_t count, u32 poshi, u32 poslo);
compat_ssize_t sys32_readahead(int fd, u32 offhi, u32 offlo, s32 count);
long sys32_sendfile(int out_fd, int in_fd, compat_off_t __user *offset,
		    size_t count);
long sys32_sendfile64(int out_fd, int in_fd, compat_loff_t __user *offset,
		      s32 count);
long sys32_sysctl(struct __sysctl_args32 __user *args);
long sys32_stat64(char __user * filename, struct stat64_emu31 __user * statbuf);
long sys32_lstat64(char __user * filename,
		   struct stat64_emu31 __user * statbuf);
long sys32_fstat64(unsigned long fd, struct stat64_emu31 __user * statbuf);
long sys32_fstatat64(unsigned int dfd, char __user *filename,
		     struct stat64_emu31 __user* statbuf, int flag);
unsigned long old32_mmap(struct mmap_arg_struct_emu31 __user *arg);
long sys32_mmap2(struct mmap_arg_struct_emu31 __user *arg);
long sys32_read(unsigned int fd, char __user * buf, size_t count);
long sys32_write(unsigned int fd, char __user * buf, size_t count);
long sys32_clone(void);
long sys32_fadvise64(int fd, loff_t offset, size_t len, int advise);
long sys32_fadvise64_64(struct fadvise64_64_args __user *args);
long sys32_sigaction(int sig, const struct old_sigaction32 __user *act,
		     struct old_sigaction32 __user *oact);
long sys32_rt_sigaction(int sig, const struct sigaction32 __user *act,
			struct sigaction32 __user *oact, size_t sigsetsize);
long sys32_sigaltstack(const stack_t32 __user *uss, stack_t32 __user *uoss);
#endif /* _ASM_S390X_S390_H */
