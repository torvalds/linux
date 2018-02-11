/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _ASM_POWERPC_SEMBUF_H
#define _ASM_POWERPC_SEMBUF_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * The semid64_ds structure for PPC architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 */

struct semid64_ds {
	struct ipc64_perm sem_perm;	/* permissions .. see ipc.h */
#ifndef __powerpc64__
	unsigned long	__unused1;
#endif
	__kernel_time_t	sem_otime;	/* last semop time */
#ifndef __powerpc64__
	unsigned long	__unused2;
#endif
	__kernel_time_t	sem_ctime;	/* last change time */
	unsigned long	sem_nsems;	/* no. of semaphores in array */
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif	/* _ASM_POWERPC_SEMBUF_H */
