/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _SPARC_SEMBUF_H
#define _SPARC_SEMBUF_H

/*
 * The semid64_ds structure for sparc architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 2 miscellaneous 32-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
#if defined(__sparc__) && defined(__arch64__)
	long		sem_otime;		/* last semop time */
	long		sem_ctime;		/* last change time */
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

#endif /* _SPARC64_SEMBUF_H */
