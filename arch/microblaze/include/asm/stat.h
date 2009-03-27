/*
 * Microblaze stat structure
 *
 * Copyright (C) 2001,02,03 NEC Electronics Corporation
 * Copyright (C) 2001,02,03 Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef _ASM_MICROBLAZE_STAT_H
#define _ASM_MICROBLAZE_STAT_H

#include <linux/posix_types.h>

struct stat {
	unsigned int	st_dev;
	unsigned long	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	st_rdev;
	unsigned long	st_size;
	unsigned long	st_blksize;
	unsigned long	st_blocks;
	unsigned long	st_atime;
	unsigned long	__unused1; /* unsigned long  st_atime_nsec */
	unsigned long	st_mtime;
	unsigned long	__unused2; /* unsigned long  st_mtime_nsec */
	unsigned long	st_ctime;
	unsigned long	__unused3; /* unsigned long  st_ctime_nsec */
	unsigned long	__unused4;
	unsigned long	__unused5;
};

struct stat64 {
	unsigned long long	st_dev;
	unsigned long	__unused1;

	unsigned long long	st_ino;

	unsigned int	st_mode;
	unsigned int	st_nlink;

	unsigned int	st_uid;
	unsigned int	st_gid;

	unsigned long long	st_rdev;
	unsigned long	__unused3;

	long long	st_size;
	unsigned long	st_blksize;

	unsigned long	st_blocks; /* No. of 512-byte blocks allocated */
	unsigned long	__unused4; /* future possible st_blocks high bits */

	unsigned long	st_atime;
	unsigned long	st_atime_nsec;

	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;

	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;

	unsigned long	__unused8;
};

#endif /* _ASM_MICROBLAZE_STAT_H */
