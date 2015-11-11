/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/statfs.h"
 */

#ifndef _S390_STATFS_H
#define _S390_STATFS_H

/*
 * We can't use <asm-generic/statfs.h> because in 64-bit mode
 * we mix ints of different sizes in our struct statfs.
 */

#ifndef __KERNEL_STRICT_NAMES
#include <linux/types.h>
typedef __kernel_fsid_t	fsid_t;
#endif

struct statfs {
	unsigned int	f_type;
	unsigned int	f_bsize;
	unsigned long	f_blocks;
	unsigned long	f_bfree;
	unsigned long	f_bavail;
	unsigned long	f_files;
	unsigned long	f_ffree;
	__kernel_fsid_t f_fsid;
	unsigned int	f_namelen;
	unsigned int	f_frsize;
	unsigned int	f_flags;
	unsigned int	f_spare[4];
};

struct statfs64 {
	unsigned int	f_type;
	unsigned int	f_bsize;
	unsigned long	f_blocks;
	unsigned long	f_bfree;
	unsigned long	f_bavail;
	unsigned long	f_files;
	unsigned long	f_ffree;
	__kernel_fsid_t f_fsid;
	unsigned int	f_namelen;
	unsigned int	f_frsize;
	unsigned int	f_flags;
	unsigned int	f_spare[4];
};

#endif
