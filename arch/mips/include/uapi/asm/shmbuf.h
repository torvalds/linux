/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_SHMBUF_H
#define _ASM_SHMBUF_H

/*
 * The shmid64_ds structure for the MIPS architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * As MIPS was lacking proper padding after shm_?time, we use 48 bits
 * of the padding at the end to store a few additional bits of the time.
 * libc implementations need to take care to convert this into a proper
 * data structure when moving to 64-bit time_t.
 */

#ifdef __mips64
struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
	long			shm_atime;	/* last attach time */
	long			shm_dtime;	/* last detach time */
	long			shm_ctime;	/* last change time */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned long		shm_nattch;	/* no. of current attaches */
	unsigned long		__unused1;
	unsigned long		__unused2;
};
#else
struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
	unsigned long		shm_atime;	/* last attach time */
	unsigned long		shm_dtime;	/* last detach time */
	unsigned long		shm_ctime;	/* last change time */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned long		shm_nattch;	/* no. of current attaches */
	unsigned short		shm_atime_high;
	unsigned short		shm_dtime_high;
	unsigned short		shm_ctime_high;
	unsigned short		__unused1;
};
#endif

struct shminfo64 {
	unsigned long	shmmax;
	unsigned long	shmmin;
	unsigned long	shmmni;
	unsigned long	shmseg;
	unsigned long	shmall;
	unsigned long	__unused1;
	unsigned long	__unused2;
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif /* _ASM_SHMBUF_H */
