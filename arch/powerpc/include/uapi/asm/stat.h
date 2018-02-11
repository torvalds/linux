/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
#ifndef _ASM_POWERPC_STAT_H
#define _ASM_POWERPC_STAT_H
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>

#define STAT_HAVE_NSEC 1

#ifndef __powerpc64__
struct __old_kernel_stat {
	unsigned short st_dev;
	unsigned short st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned long  st_size;
	unsigned long  st_atime;
	unsigned long  st_mtime;
	unsigned long  st_ctime;
};
#endif /* !__powerpc64__ */

struct stat {
	unsigned long	st_dev;
	ino_t		st_ino;
#ifdef __powerpc64__
	unsigned long	st_nlink;
	mode_t		st_mode;
#else
	mode_t		st_mode;
	unsigned short	st_nlink;
#endif
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned long	st_rdev;
	off_t		st_size;
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
#ifdef __powerpc64__
	unsigned long	__unused6;
#endif
};

/* This matches struct stat64 in glibc2.1. Only used for 32 bit. */
struct stat64 {
	unsigned long long st_dev;		/* Device.  */
	unsigned long long st_ino;		/* File serial number.  */
	unsigned int	st_mode;	/* File mode.  */
	unsigned int	st_nlink;	/* Link count.  */
	unsigned int	st_uid;		/* User ID of the file's owner.  */
	unsigned int	st_gid;		/* Group ID of the file's group. */
	unsigned long long st_rdev;	/* Device number, if device.  */
	unsigned short	__pad2;
	long long	st_size;	/* Size of file, in bytes.  */
	int		st_blksize;	/* Optimal block size for I/O.  */
	long long	st_blocks;	/* Number 512-byte blocks allocated. */
	int		st_atime;	/* Time of last access.  */
	unsigned int	st_atime_nsec;
	int		st_mtime;	/* Time of last modification.  */
	unsigned int	st_mtime_nsec;
	int		st_ctime;	/* Time of last status change.  */
	unsigned int	st_ctime_nsec;
	unsigned int	__unused4;
	unsigned int	__unused5;
};

#endif /* _ASM_POWERPC_STAT_H */
