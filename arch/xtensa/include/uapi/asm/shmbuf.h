/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/asm-xtensa/shmbuf.h
 *
 * The shmid64_ds structure for Xtensa architecture.
 * Note extra padding because this structure is passed back and forth
 * between kernel and user space, but the padding is on the wrong
 * side for big-endian xtensa, for historic reasons.
 *
 * Pad space is left for:
 * - 2 miscellaneous 32-bit values
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SHMBUF_H
#define _XTENSA_SHMBUF_H

#include <asm/ipcbuf.h>
#include <asm/posix_types.h>

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* operation perms */
	__kernel_size_t		shm_segsz;	/* size of segment (bytes) */
	unsigned long		shm_atime;	/* last attach time */
	unsigned long		shm_atime_high;
	unsigned long		shm_dtime;	/* last detach time */
	unsigned long		shm_dtime_high;
	unsigned long		shm_ctime;	/* last change time */
	unsigned long		shm_ctime_high;
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

#endif	/* _XTENSA_SHMBUF_H */
