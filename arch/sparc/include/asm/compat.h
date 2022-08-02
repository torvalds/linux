/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SPARC64_COMPAT_H
#define _ASM_SPARC64_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>

#define compat_mode_t	compat_mode_t
typedef u16		compat_mode_t;

#define __compat_uid_t	__compat_uid_t
typedef u16		__compat_uid_t;
typedef u16		__compat_gid_t;

#define compat_dev_t	compat_dev_t
typedef u16		compat_dev_t;

#define compat_ipc_pid_t compat_ipc_pid_t
typedef u16		 compat_ipc_pid_t;

#define compat_ipc64_perm compat_ipc64_perm

#define COMPAT_RLIM_INFINITY 0x7fffffff

#include <asm-generic/compat.h>

#define COMPAT_UTS_MACHINE	"sparc\0\0"

typedef s16		compat_nlink_t;

struct compat_stat {
	compat_dev_t	st_dev;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	__compat_uid_t	st_uid;
	__compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	compat_off_t	st_size;
	old_time32_t	st_atime;
	compat_ulong_t	st_atime_nsec;
	old_time32_t	st_mtime;
	compat_ulong_t	st_mtime_nsec;
	old_time32_t	st_ctime;
	compat_ulong_t	st_ctime_nsec;
	compat_off_t	st_blksize;
	compat_off_t	st_blocks;
	u32		__unused4[2];
};

struct compat_stat64 {
	unsigned long long	st_dev;

	unsigned long long	st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned int	st_uid;
	unsigned int	st_gid;

	unsigned long long	st_rdev;

	unsigned char	__pad3[8];

	long long	st_size;
	unsigned int	st_blksize;

	unsigned char	__pad4[8];
	unsigned int	st_blocks;

	unsigned int	st_atime;
	unsigned int	st_atime_nsec;

	unsigned int	st_mtime;
	unsigned int	st_mtime_nsec;

	unsigned int	st_ctime;
	unsigned int	st_ctime_nsec;

	unsigned int	__unused4;
	unsigned int	__unused5;
};

#define __ARCH_COMPAT_FLOCK_PAD		short __unused;

struct compat_ipc64_perm {
	compat_key_t key;
	__compat_uid32_t uid;
	__compat_gid32_t gid;
	__compat_uid32_t cuid;
	__compat_gid32_t cgid;
	unsigned short __pad1;
	compat_mode_t mode;
	unsigned short __pad2;
	unsigned short seq;
	unsigned long __unused1;	/* yes they really are 64bit pads */
	unsigned long __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	unsigned int	sem_otime_high;
	unsigned int	sem_otime;
	unsigned int	sem_ctime_high;
	unsigned int	sem_ctime;
	u32		sem_nsems;
	u32		__unused1;
	u32		__unused2;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	unsigned int	msg_stime_high;
	unsigned int	msg_stime;
	unsigned int	msg_rtime_high;
	unsigned int	msg_rtime;
	unsigned int	msg_ctime_high;
	unsigned int	msg_ctime;
	unsigned int	msg_cbytes;
	unsigned int	msg_qnum;
	unsigned int	msg_qbytes;
	compat_pid_t	msg_lspid;
	compat_pid_t	msg_lrpid;
	unsigned int	__unused1;
	unsigned int	__unused2;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	unsigned int	shm_atime_high;
	unsigned int	shm_atime;
	unsigned int	shm_dtime_high;
	unsigned int	shm_dtime;
	unsigned int	shm_ctime_high;
	unsigned int	shm_ctime;
	compat_size_t	shm_segsz;
	compat_pid_t	shm_cpid;
	compat_pid_t	shm_lpid;
	unsigned int	shm_nattch;
	unsigned int	__unused1;
	unsigned int	__unused2;
};

#ifdef CONFIG_COMPAT
static inline int is_compat_task(void)
{
	return test_thread_flag(TIF_32BIT);
}

static inline bool in_compat_syscall(void)
{
	/* Vector 0x110 is LINUX_32BIT_SYSCALL_TRAP */
	return pt_regs_trap_type(current_pt_regs()) == 0x110;
}
#define in_compat_syscall in_compat_syscall
#endif

#endif /* _ASM_SPARC64_COMPAT_H */
