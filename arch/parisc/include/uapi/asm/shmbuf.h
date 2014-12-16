#ifndef _PARISC_SHMBUF_H
#define _PARISC_SHMBUF_H

#include <asm/bitsperlong.h>

/* 
 * The shmid64_ds structure for parisc architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
#if __BITS_PER_LONG != 64
	unsigned int		__pad1;
#endif
	__kernel_time_t		shm_atime;	/* last attach time */
#if __BITS_PER_LONG != 64
	unsigned int		__pad2;
#endif
	__kernel_time_t		shm_dtime;	/* last detach time */
#if __BITS_PER_LONG != 64
	unsigned int		__pad3;
#endif
	__kernel_time_t		shm_ctime;	/* last change time */
#if __BITS_PER_LONG != 64
	unsigned int		__pad4;
#endif
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned int		shm_nattch;	/* no. of current attaches */
	unsigned int		__unused1;
	unsigned int		__unused2;
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

#endif /* _PARISC_SHMBUF_H */
