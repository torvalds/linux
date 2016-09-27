/*
 * Code specific to UniCore32 ISA
 *
 * Copyright (C) 2014 GUAN Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE32_ASM_STAT_OLDABI_H__
#define __UNICORE32_ASM_STAT_OLDABI_H__

#define STAT_HAVE_NSEC 1

struct stat {
	unsigned long	st_dev;		/* Device.  */
	unsigned long	st_ino;		/* File serial number.  */
	unsigned int	st_mode;	/* File mode.  */
	unsigned int	st_nlink;	/* Link count.  */
	unsigned int	st_uid;		/* User ID of the file's owner.  */
	unsigned int	st_gid;		/* Group ID of the file's group. */
	unsigned long	st_rdev;	/* Device number, if device.  */
	unsigned long	__pad1;
	long		st_size;	/* Size of file, in bytes.  */
	int		st_blksize;	/* Optimal block size for I/O.  */
	int		__pad2;
	long		st_blocks;	/* Number 512-byte blocks allocated. */
	int		st_atime;	/* Time of last access.  */
	unsigned int	st_atime_nsec;
	int		st_mtime;	/* Time of last modification.  */
	unsigned int	st_mtime_nsec;
	int		st_ctime;	/* Time of last status change.  */
	unsigned int	st_ctime_nsec;
	unsigned int	__unused4;
	unsigned int	__unused5;
};

/*
 * This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 * Note: The kernel zero's the padded region because glibc might read them
 * in the hope that the kernel has stretched to using larger sizes.
 */
#define STAT64_HAS_BROKEN_ST_INO

struct stat64 {
	unsigned long long	st_dev;		/* Device.  */
	unsigned char		__pad0[4];
	unsigned long		__st_ino;
	unsigned int		st_mode;	/* File mode.  */
	unsigned int		st_nlink;	/* Link count.  */
	unsigned int		st_uid;		/* UID of the file's owner.  */
	unsigned int		st_gid;		/* GID of the file's group. */
	unsigned long long	st_rdev;	/* Device number, if device. */

	unsigned char   __pad3[4];

	long long	st_size;	/* Size of file, in bytes.  */
	int		st_blksize;	/* Optimal block size for I/O.  */
	long long	st_blocks;	/* Number 512-byte blocks allocated. */
	int		st_atime;	/* Time of last access.  */
	unsigned int	st_atime_nsec;
	int		st_mtime;	/* Time of last modification.  */
	unsigned int	st_mtime_nsec;
	int		st_ctime;	/* Time of last status change.  */
	unsigned int	st_ctime_nsec;

	unsigned long long	st_ino;
};

#endif /* __UNICORE32_ASM_STAT_OLDABI_H__ */
