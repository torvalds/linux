/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _PARISC_SEMBUF_H
#define _PARISC_SEMBUF_H

#include <asm/bitsperlong.h>

/* 
 * The semid64_ds structure for parisc architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 2 miscellaneous 32-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
#if __BITS_PER_LONG == 64
	__kernel_time_t	sem_otime;		/* last semop time */
	__kernel_time_t	sem_ctime;		/* last change time */
#else
	unsigned long	sem_otime_high;
	unsigned long	sem_otime;		/* last semop time */
	unsigned long	sem_ctime_high;
	unsigned long	sem_ctime;		/* last change time */
#endif
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused1;
	unsigned long	__unused2;
};

#endif /* _PARISC_SEMBUF_H */
