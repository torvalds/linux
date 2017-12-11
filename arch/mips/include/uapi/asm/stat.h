/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999, 2000 Ralf Baechle
 * Copyright (C) 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_STAT_H
#define _ASM_STAT_H

#include <linux/types.h>

#include <asm/sgidefs.h>

#if (_MIPS_SIM == _MIPS_SIM_ABI32) || (_MIPS_SIM == _MIPS_SIM_NABI32)

struct stat {
	unsigned	st_dev;
	long		st_pad1[3];		/* Reserved for network id */
	ino_t		st_ino;
	mode_t		st_mode;
	__u32		st_nlink;
	uid_t		st_uid;
	gid_t		st_gid;
	unsigned	st_rdev;
	long		st_pad2[2];
	off_t		st_size;
	long		st_pad3;
	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	time_t		st_atime;
	long		st_atime_nsec;
	time_t		st_mtime;
	long		st_mtime_nsec;
	time_t		st_ctime;
	long		st_ctime_nsec;
	long		st_blksize;
	long		st_blocks;
	long		st_pad4[14];
};

/*
 * This matches struct stat64 in glibc2.1, hence the absolutely insane
 * amounts of padding around dev_t's.  The memory layout is the same as of
 * struct stat of the 64-bit kernel.
 */

struct stat64 {
	unsigned long	st_dev;
	unsigned long	st_pad0[3];	/* Reserved for st_dev expansion  */

	unsigned long long	st_ino;

	mode_t		st_mode;
	__u32		st_nlink;

	uid_t		st_uid;
	gid_t		st_gid;

	unsigned long	st_rdev;
	unsigned long	st_pad1[3];	/* Reserved for st_rdev expansion  */

	long long	st_size;

	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	time_t		st_atime;
	unsigned long	st_atime_nsec;	/* Reserved for st_atime expansion  */

	time_t		st_mtime;
	unsigned long	st_mtime_nsec;	/* Reserved for st_mtime expansion  */

	time_t		st_ctime;
	unsigned long	st_ctime_nsec;	/* Reserved for st_ctime expansion  */

	unsigned long	st_blksize;
	unsigned long	st_pad2;

	long long	st_blocks;
};

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#if _MIPS_SIM == _MIPS_SIM_ABI64

/* The memory layout is the same as of struct stat64 of the 32-bit kernel.  */
struct stat {
	unsigned int		st_dev;
	unsigned int		st_pad0[3]; /* Reserved for st_dev expansion */

	unsigned long		st_ino;

	mode_t			st_mode;
	__u32			st_nlink;

	uid_t			st_uid;
	gid_t			st_gid;

	unsigned int		st_rdev;
	unsigned int		st_pad1[3]; /* Reserved for st_rdev expansion */

	off_t			st_size;

	/*
	 * Actually this should be timestruc_t st_atime, st_mtime and st_ctime
	 * but we don't have it under Linux.
	 */
	unsigned int		st_atime;
	unsigned int		st_atime_nsec;

	unsigned int		st_mtime;
	unsigned int		st_mtime_nsec;

	unsigned int		st_ctime;
	unsigned int		st_ctime_nsec;

	unsigned int		st_blksize;
	unsigned int		st_pad2;

	unsigned long		st_blocks;
};

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 */

#define STAT_HAVE_NSEC 1

#endif /* _ASM_STAT_H */
