/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_SEMBUF_H
#define _ASM_SEMBUF_H

/*
 * The semid64_ds structure for the MIPS architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for 2 miscellaneous 64-bit values on mips64,
 * but used for the upper 32 bit of the time values on mips32.
 */

#ifdef __mips64
struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
	long		 sem_otime;		/* last semop time */
	long		 sem_ctime;		/* last change time */
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused1;
	unsigned long	__unused2;
};
#else
struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
	unsigned long   sem_otime;		/* last semop time */
	unsigned long   sem_ctime;		/* last change time */
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	sem_otime_high;
	unsigned long	sem_ctime_high;
};
#endif

#endif /* _ASM_SEMBUF_H */
