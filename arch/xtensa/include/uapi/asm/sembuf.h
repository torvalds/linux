/*
 * include/asm-xtensa/sembuf.h
 *
 * The semid64_ds structure for Xtensa architecture.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space.
 *
 * Pad space is left for:
 * - 64-bit time_t to solve y2038 problem
 * - 2 miscellaneous 32-bit values
 *
 */

#ifndef _XTENSA_SEMBUF_H
#define _XTENSA_SEMBUF_H

#include <asm/byteorder.h>

struct semid64_ds {
	struct ipc64_perm sem_perm;		/* permissions .. see ipc.h */
#ifdef __XTENSA_EL__
	__kernel_time_t	sem_otime;		/* last semop time */
	unsigned long	__unused1;
	__kernel_time_t	sem_ctime;		/* last change time */
	unsigned long	__unused2;
#else
	unsigned long	__unused1;
	__kernel_time_t	sem_otime;		/* last semop time */
	unsigned long	__unused2;
	__kernel_time_t	sem_ctime;		/* last change time */
#endif
	unsigned long	sem_nsems;		/* no. of semaphores in array */
	unsigned long	__unused3;
	unsigned long	__unused4;
};

#endif /* __ASM_XTENSA_SEMBUF_H */
