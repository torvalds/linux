/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _SPARC_SHMBUF_H
#define _SPARC_SHMBUF_H

/* 
 * The shmid64_ds structure for sparc architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

#if defined(__sparc__) && defined(__arch64__)
# define PADDING(x)
#else
# define PADDING(x) unsigned int x;
#endif

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	PADDING(__pad1)
	__kernel_time_t		shm_atime;	/* last attach time */
	PADDING(__pad2)
	__kernel_time_t		shm_dtime;	/* last detach time */
	PADDING(__pad3)
	__kernel_time_t		shm_ctime;	/* last change time */
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned long		shm_nattch;	/* no. of current attaches */
	unsigned long		__unused1;
	unsigned long		__unused2;
};

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

#undef PADDING

#endif /* _SPARC_SHMBUF_H */
