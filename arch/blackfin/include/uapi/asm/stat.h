/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2004-2006 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#ifndef _UAPI_BFIN_STAT_H
#define _UAPI_BFIN_STAT_H

struct stat {
	unsigned short st_dev;
	unsigned short __pad1;
	unsigned long st_ino;
	unsigned short st_mode;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
	unsigned short st_rdev;
	unsigned short __pad2;
	unsigned long st_size;
	unsigned long st_blksize;
	unsigned long st_blocks;
	unsigned long st_atime;
	unsigned long __unused1;
	unsigned long st_mtime;
	unsigned long __unused2;
	unsigned long st_ctime;
	unsigned long __unused3;
	unsigned long __unused4;
	unsigned long __unused5;
};

/* This matches struct stat64 in glibc2.1, hence the absolutely
 * insane amounts of padding around dev_t's.
 */
struct stat64 {
	unsigned long long st_dev;
	unsigned char __pad1[4];

#define STAT64_HAS_BROKEN_ST_INO	1
	unsigned long __st_ino;

	unsigned int st_mode;
	unsigned int st_nlink;

	unsigned long st_uid;
	unsigned long st_gid;

	unsigned long long st_rdev;
	unsigned char __pad2[4];

	long long st_size;
	unsigned long st_blksize;

	long long st_blocks;	/* Number 512-byte blocks allocated. */

	unsigned long st_atime;
	unsigned long st_atime_nsec;

	unsigned long st_mtime;
	unsigned long st_mtime_nsec;

	unsigned long st_ctime;
	unsigned long st_ctime_nsec;

	unsigned long long st_ino;
};

#endif /* _UAPI_BFIN_STAT_H */
