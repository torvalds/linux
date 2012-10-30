#ifndef _S390_SEMBUF_H
#define _S390_SEMBUF_H

/* 
 * The semid64_ds structure for S/390 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem (for !__s390x__)
 * - 2 miscellaneous 32-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;		/* last semop time */
#ifndef __s390x__
	unsigned long	__unused1;
#endif /* ! __s390x__ */
	__kernel_time_t	sem_ctime;		/* last change time */
#ifndef __s390x__
	unsigned long	__unused2;
#endif /* ! __s390x__ */
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif /* _S390_SEMBUF_H */
