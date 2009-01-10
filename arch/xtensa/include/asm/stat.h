/*
 * include/asm-xtensa/stat.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 */

#ifndef _XTENSA_STAT_H
#define _XTENSA_STAT_H

#define STAT_HAVE_NSEC 1

struct stat {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned long	st_rdev;
	long		st_size;
	unsigned long	st_blksize;
	unsigned long	st_blocks;
	unsigned long	st_atime;
	unsigned long	st_atime_nsec;
	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctime;
	unsigned long	st_ctime_nsec;
	unsigned long	__unused4;
	unsigned long	__unused5;
};

struct stat64  {
	unsigned long long st_dev;	/* Device */
	unsigned long long st_ino;	/* File serial number */
	unsigned int  st_mode;		/* File mode. */
	unsigned int  st_nlink;		/* Link count. */
	unsigned int  st_uid;		/* User ID of the file's owner. */
	unsigned int  st_gid;		/* Group ID of the file's group. */
	unsigned long long st_rdev;	/* Device number, if device. */
	long long st_size;		/* Size of file, in bytes. */
	unsigned long st_blksize;	/* Optimal block size for I/O. */
	unsigned long __unused2;
	unsigned long long st_blocks;	/* Number 512-byte blocks allocated. */
	unsigned long st_atime;		/* Time of last access. */
	unsigned long st_atime_nsec;
	unsigned long st_mtime;		/* Time of last modification. */
	unsigned long st_mtime_nsec;
	unsigned long st_ctime;		/* Time of last status change. */
	unsigned long st_ctime_nsec;
	unsigned long __unused4;
	unsigned long __unused5;
};

#endif	/* _XTENSA_STAT_H */
