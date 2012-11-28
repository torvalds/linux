#ifndef _PARISC_SHMBUF_H
#define _PARISC_SHMBUF_H

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
#ifndef CONFIG_64BIT
	unsigned int		__pad1;
#endif
	__kernel_time_t		shm_atime;	/* last attach time */
#ifndef CONFIG_64BIT
	unsigned int		__pad2;
#endif
	__kernel_time_t		shm_dtime;	/* last detach time */
#ifndef CONFIG_64BIT
	unsigned int		__pad3;
#endif
	__kernel_time_t		shm_ctime;	/* last change time */
#ifndef CONFIG_64BIT
	unsigned int		__pad4;
#endif
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	unsigned int		shm_nattch;	/* no. of current attaches */
	unsigned int		__unused1;
	unsigned int		__unused2;
};

#ifdef CONFIG_64BIT
/* The 'unsigned int' (formerly 'unsigned long') data types below will
 * ensure that a 32-bit app calling shmctl(*,IPC_INFO,*) will work on
 * a wide kernel, but if some of these values are meant to contain pointers
 * they may need to be 'long long' instead. -PB XXX FIXME
 */
#endif
struct shminfo64 {
	unsigned int	shmmax;
	unsigned int	shmmin;
	unsigned int	shmmni;
	unsigned int	shmseg;
	unsigned int	shmall;
	unsigned int	__unused1;
	unsigned int	__unused2;
	unsigned int	__unused3;
	unsigned int	__unused4;
};

#endif /* _PARISC_SHMBUF_H */
