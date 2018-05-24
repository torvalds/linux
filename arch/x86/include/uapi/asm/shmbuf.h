/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_X86_SHMBUF_H
#define __ASM_X86_SHMBUF_H

#if !defined(__x86_64__) || !defined(__ILP32__)
#include <asm-generic/shmbuf.h>
#else
/*
 * The shmid64_ds structure for x86 architecture with x32 ABI.
 *
 * On x86-32 and x86-64 we can just use the generic definition, but
 * x32 uses the same binary layout as x86_64, which is differnet
 * from other 32-bit architectures.
 */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	size_t			shm_segsz;	/* size of segment (bytes) */
	__kernel_time_t		shm_atime;	/* last attach time */
	__kernel_time_t		shm_dtime;	/* last detach time */
	__kernel_time_t		shm_ctime;	/* last change time */
	__kernel_pid_t		shm_cpid;	/* pid of creator */
	__kernel_pid_t		shm_lpid;	/* pid of last operator */
	__kernel_ulong_t	shm_nattch;	/* no. of current attaches */
	__kernel_ulong_t	__unused4;
	__kernel_ulong_t	__unused5;
};

struct shminfo64 {
	__kernel_ulong_t	shmmax;
	__kernel_ulong_t	shmmin;
	__kernel_ulong_t	shmmni;
	__kernel_ulong_t	shmseg;
	__kernel_ulong_t	shmall;
	__kernel_ulong_t	__unused1;
	__kernel_ulong_t	__unused2;
	__kernel_ulong_t	__unused3;
	__kernel_ulong_t	__unused4;
};

#endif

#endif /* __ASM_X86_SHMBUF_H */
