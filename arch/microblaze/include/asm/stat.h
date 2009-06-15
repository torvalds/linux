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

#define STAT_HAVE_NSEC 1

struct stat {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned long	st_rdev;
	unsigned long	__pad1;
	long		st_size;
	int		st_blksize;
	int		__pad2;
	long		st_blocks;
	int		st_atime;
	unsigned int	st_atime_nsec;
	int		st_mtime;
	unsigned int	st_mtime_nsec;
	int		st_ctime;
	unsigned int	st_ctime_nsec;
	unsigned long	__unused4;
	unsigned long	__unused5;
};

struct stat64 {
	unsigned long long	st_dev;		/* Device.  */
	unsigned long long	st_ino;		/* File serial number.  */
	unsigned int		st_mode;	/* File mode.  */
	unsigned int		st_nlink;	/* Link count.  */
	unsigned int		st_uid;		/* User ID of the file's owner.  */
	unsigned int		st_gid;		/* Group ID of the file's group. */
	unsigned long long	st_rdev;	/* Device number, if device.  */
	unsigned long long	__pad1;
	long long		st_size;	/* Size of file, in bytes.  */
	int			st_blksize;	/* Optimal block size for I/O.  */
	int			__pad2;
	long long		st_blocks;	/* Number 512-byte blocks allocated. */
	int			st_atime;	/* Time of last access.  */
	unsigned int		st_atime_nsec;
	int			st_mtime;	/* Time of last modification.  */
	unsigned int		st_mtime_nsec;
	int			st_ctime;	/* Time of last status change.  */
	unsigned int		st_ctime_nsec;
	unsigned int		__unused4;
	unsigned int		__unused5;
};

#endif /* _ASM_MICROBLAZE_STAT_H */

