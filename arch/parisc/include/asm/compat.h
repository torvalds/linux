/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_COMPAT_H
#define _ASM_PARISC_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/thread_info.h>

#include <asm-generic/compat.h>

#define COMPAT_USER_HZ 		100
#define COMPAT_UTS_MACHINE	"parisc\0\0"

typedef u32	__compat_uid_t;
typedef u32	__compat_gid_t;
typedef u32	__compat_uid32_t;
typedef u32	__compat_gid32_t;
typedef u16	compat_mode_t;
typedef u32	compat_dev_t;
typedef u16	compat_nlink_t;
typedef u16	compat_ipc_pid_t;
typedef u32	compat_caddr_t;

struct compat_stat {
	compat_dev_t		st_dev;	/* dev_t is 32 bits on parisc */
	compat_ino_t		st_ino;	/* 32 bits */
	compat_mode_t		st_mode;	/* 16 bits */
	compat_nlink_t  	st_nlink;	/* 16 bits */
	u16			st_reserved1;	/* old st_uid */
	u16			st_reserved2;	/* old st_gid */
	compat_dev_t		st_rdev;
	compat_off_t		st_size;
	old_time32_t		st_atime;
	u32			st_atime_nsec;
	old_time32_t		st_mtime;
	u32			st_mtime_nsec;
	old_time32_t		st_ctime;
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

#define COMPAT_OFF_T_MAX	0x7fffffff

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
	unsigned int sem_otime_high;
	unsigned int sem_otime;
	unsigned int sem_ctime_high;
	unsigned int sem_ctime;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused3;
	compat_ulong_t __unused4;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	unsigned int msg_stime_high;
	unsigned int msg_stime;
	unsigned int msg_rtime_high;
	unsigned int msg_rtime;
	unsigned int msg_ctime_high;
	unsigned int msg_ctime;
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
	unsigned int shm_atime_high;
	unsigned int shm_atime;
	unsigned int shm_dtime_high;
	unsigned int shm_dtime;
	unsigned int shm_ctime_high;
	unsigned int shm_ctime;
	unsigned int __unused4;
	compat_size_t shm_segsz;
	compat_pid_t shm_cpid;
	compat_pid_t shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused5;
	compat_ulong_t __unused6;
};

/*
 * The type of struct elf_prstatus.pr_reg in compatible core dumps.
 */
#define COMPAT_ELF_NGREG 80
typedef compat_ulong_t compat_elf_gregset_t[COMPAT_ELF_NGREG];

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
