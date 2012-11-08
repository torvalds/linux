#ifndef _PARISC_SEMBUF_H
#define _PARISC_SEMBUF_H

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
#ifndef CONFIG_64BIT
	unsigned int	__pad1;
#endif
	__kernel_time_t	sem_otime;		/* last semop time */
#ifndef CONFIG_64BIT
	unsigned int	__pad2;
#endif
	__kernel_time_t	sem_ctime;		/* last change time */
	unsigned int	sem_nsems;		/* no. of semaphores in array */
	unsigned int	__unused1;
	unsigned int	__unused2;
};

#endif /* _PARISC_SEMBUF_H */
