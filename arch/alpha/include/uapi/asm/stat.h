/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ALPHA_STAT_H
#define _ALPHA_STAT_H

struct stat {
	unsigned int	st_dev;
	unsigned int	st_ino;
	unsigned int	st_mode;
	unsigned int	st_nlink;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	st_rdev;
	long		st_size;
	unsigned long	st_atime;
	unsigned long	st_mtime;
	unsigned long	st_ctime;
	unsigned int	st_blksize;
	unsigned int	st_blocks;
	unsigned int	st_flags;
	unsigned int	st_gen;
};

/* The stat64 structure increases the size of dev_t, blkcnt_t, adds
   nanosecond resolution times, and padding for expansion.  */

struct stat64 {
	unsigned long	st_dev;
	unsigned long	st_ino;
	unsigned long	st_rdev;
	long		st_size;
	unsigned long	st_blocks;

	unsigned int	st_mode;
	unsigned int	st_uid;
	unsigned int	st_gid;
	unsigned int	st_blksize;
	unsigned int	st_nlink;
	unsigned int	__pad0;

	unsigned long	st_atime;
	unsigned long 	st_atime_nsec; 
	unsigned long	st_mtime;
	unsigned long	st_mtime_nsec;
	unsigned long	st_ctime;
	unsigned long   st_ctime_nsec;
  	long		__unused[3];
};

#endif
