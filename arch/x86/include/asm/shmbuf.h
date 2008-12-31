#ifndef _ASM_X86_SHMBUF_H
#define _ASM_X86_SHMBUF_H

/*
 * The shmid64_ds structure for x86 architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space on 32 bit is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 *
 * Pad space on 64 bit is left for:
 * - 2 miscellaneous 64-bit values
 */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_time_t		shm_atime;	/* last attach time */
#ifdef __i386__
	unsigned long		__unused1;
#endif
	__kernel_time_t		shm_dtime;	/* last detach time */
#ifdef __i386__
	unsigned long		__unused2;
#endif
	__kernel_time_t		shm_ctime;	/* last change time */
#ifdef __i386__
	unsigned long		__unused3;
#endif
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned long		shm_nattch;	/* no. of current attaches */
	unsigned long		__unused4;
	unsigned long		__unused5;
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

#endif /* _ASM_X86_SHMBUF_H */
