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
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
#if __BITS_PER_LONG != 64
	unsigned int	__pad1;
#endif
	__kernel_time_t	sem_otime;		/* last semop time */
#if __BITS_PER_LONG != 64
	unsigned int	__pad2;
#endif
	__kernel_time_t	sem_ctime;		/* last change time */
	unsigned long 	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused1;
	unsigned long	__unused2;
};

#endif /* _PARISC_SEMBUF_H */
