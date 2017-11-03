/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _SPARC_SEMBUF_H
#define _SPARC_SEMBUF_H

/*
 * The semid64_ds structure for sparc architecture.
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

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
	PADDING(__pad1)
	__kernel_time_t	sem_otime;		/* last semop time */
	PADDING(__pad2)
	__kernel_time_t	sem_ctime;		/* last change time */
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused1;
	unsigned long	__unused2;
};
#undef PADDING

#endif /* _SPARC64_SEMBUF_H */
