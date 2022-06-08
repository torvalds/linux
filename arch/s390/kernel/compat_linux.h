/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390X_S390_H
#define _ASM_S390X_S390_H

#include <linux/compat.h>
#include <linux/socket.h>
#include <linux/syscalls.h>
#include <asm/ptrace.h>

/*
 * Macro that masks the high order bit of a 32 bit pointer and
 * converts it to a 64 bit pointer.
 */
#define A(__x)	((unsigned long)((__x) & 0x7FFFFFFFUL))
#define AA(__x)	((unsigned long)(__x))

/* Now 32bit compatibility types */
struct ipc_kludge_32 {
	__u32	msgp;	/* pointer */
	__s32	msgtyp;
};

/* asm/sigcontext.h */
typedef union {
	__u64	d;
	__u32	f;
} freg_t32;

typedef struct {
	unsigned int	fpc;
	unsigned int	pad;
	freg_t32	fprs[__NUM_FPRS];
} _s390_fp_regs32;

typedef struct {
	psw_t32		psw;
	__u32		gprs[__NUM_GPRS];
	__u32		acrs[__NUM_ACRS];
} _s390_regs_common32;

typedef struct {
	_s390_regs_common32 regs;
	_s390_fp_regs32	    fpregs;
} _sigregs32;

typedef struct {
	__u32		gprs_high[__NUM_GPRS];
	__u64		vxrs_low[__NUM_VXRS_LOW];
	__vector128	vxrs_high[__NUM_VXRS_HIGH];
	__u8		__reserved[128];
} _sigregs_ext32;

#define _SIGCONTEXT_NSIG32	64
#define _SIGCONTEXT_NSIG_BPW32	32
#define __SIGNAL_FRAMESIZE32	96
#define _SIGMASK_COPY_SIZE32	(sizeof(u32) * 2)

struct sigcontext32 {
	__u32	oldmask[_COMPAT_NSIG_WORDS];
	__u32	sregs;	/* pointer */
};

/* asm/signal.h */

/* asm/ucontext.h */
struct ucontext32 {
	__u32			uc_flags;
	__u32			uc_link;	/* pointer */
	compat_stack_t		uc_stack;
	_sigregs32		uc_mcontext;
	compat_sigset_t		uc_sigmask;
	/* Allow for uc_sigmask growth. Glibc uses a 1024-bit sigset_t. */
	unsigned char		__unused[128 - sizeof(compat_sigset_t)];
	_sigregs_ext32		uc_mcontext_ext;
};

struct stat64_emu31;
struct mmap_arg_struct_emu31;
struct fadvise64_64_args;

long compat_sys_s390_truncate64(const char __user *path, u32 high, u32 low);
long compat_sys_s390_ftruncate64(unsigned int fd, u32 high, u32 low);
long compat_sys_s390_pread64(unsigned int fd, char __user *ubuf, compat_size_t count, u32 high, u32 low);
long compat_sys_s390_pwrite64(unsigned int fd, const char __user *ubuf, compat_size_t count, u32 high, u32 low);
long compat_sys_s390_readahead(int fd, u32 high, u32 low, s32 count);
long compat_sys_s390_stat64(const char __user *filename, struct stat64_emu31 __user *statbuf);
long compat_sys_s390_lstat64(const char __user *filename, struct stat64_emu31 __user *statbuf);
long compat_sys_s390_fstat64(unsigned int fd, struct stat64_emu31 __user *statbuf);
long compat_sys_s390_fstatat64(unsigned int dfd, const char __user *filename, struct stat64_emu31 __user *statbuf, int flag);
long compat_sys_s390_old_mmap(struct mmap_arg_struct_emu31 __user *arg);
long compat_sys_s390_mmap2(struct mmap_arg_struct_emu31 __user *arg);
long compat_sys_s390_read(unsigned int fd, char __user *buf, compat_size_t count);
long compat_sys_s390_write(unsigned int fd, const char __user *buf, compat_size_t count);
long compat_sys_s390_fadvise64(int fd, u32 high, u32 low, compat_size_t len, int advise);
long compat_sys_s390_fadvise64_64(struct fadvise64_64_args __user *args);
long compat_sys_s390_sync_file_range(int fd, u32 offhigh, u32 offlow, u32 nhigh, u32 nlow, unsigned int flags);
long compat_sys_s390_fallocate(int fd, int mode, u32 offhigh, u32 offlow, u32 lenhigh, u32 lenlow);
long compat_sys_sigreturn(void);
long compat_sys_rt_sigreturn(void);

#endif /* _ASM_S390X_S390_H */
