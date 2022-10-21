/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_COMPAT_H
#define _ASM_POWERPC_COMPAT_H
#ifdef __KERNEL__
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>

#define compat_ipc_pid_t compat_ipc_pid_t
typedef u16		compat_ipc_pid_t;

#define compat_ipc64_perm compat_ipc64_perm

#include <asm-generic/compat.h>

#ifdef __BIG_ENDIAN__
#define COMPAT_UTS_MACHINE	"ppc\0\0"
#else
#define COMPAT_UTS_MACHINE	"ppcle\0\0"
#endif

typedef s16		compat_nlink_t;

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
	old_time32_t	st_atime;
	u32		st_atime_nsec;
	old_time32_t	st_mtime;
	u32		st_mtime_nsec;
	old_time32_t	st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4[2];
};

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

static inline int is_compat_task(void)
{
	return is_32bit_task();
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_COMPAT_H */
