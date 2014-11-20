#ifndef _ASM_X86_SEMBUF_H
#define _ASM_X86_SEMBUF_H

/*
 * The semid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */
struct semid64_ds {
	struct ipc64_perm sem_perm;	/* permissions .. see ipc.h */
	__kernel_time_t	sem_otime;	/* last semop time */
	__kernel_ulong_t __unused1;
	__kernel_time_t	sem_ctime;	/* last change time */
	__kernel_ulong_t __unused2;
	__kernel_ulong_t sem_nsems;	/* no. of semaphores in array */
	__kernel_ulong_t __unused3;
	__kernel_ulong_t __unused4;
};

#endif /* _ASM_X86_SEMBUF_H */
