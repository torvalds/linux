/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/statfs.h"
 */

#ifndef _S390_STATFS_H
#define _S390_STATFS_H

#ifndef __s390x__
#include <asm-generic/statfs.h>
#else
/*
 * We can't use <asm-generic/statfs.h> because in 64-bit mode
 * we mix ints of different sizes in our struct statfs.
 */

#ifndef __KERNEL_STRICT_NAMES
#include <linux/types.h>
typedef __kernel_fsid_t	fsid_t;
#endif

struct statfs {
	int  f_type;
	int  f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	int  f_namelen;
	int  f_frsize;
	int  f_flags;
	int  f_spare[4];
};

struct statfs64 {
	int  f_type;
	int  f_bsize;
	long f_blocks;
	long f_bfree;
	long f_bavail;
	long f_files;
	long f_ffree;
	__kernel_fsid_t f_fsid;
	int  f_namelen;
	int  f_frsize;
	int  f_flags;
	int  f_spare[4];
};

struct compat_statfs64 {
	__u32 f_type;
	__u32 f_bsize;
	__u64 f_blocks;
	__u64 f_bfree;
	__u64 f_bavail;
	__u64 f_files;
	__u64 f_ffree;
	__kernel_fsid_t f_fsid;
	__u32 f_namelen;
	__u32 f_frsize;
	__u32 f_flags;
	__u32 f_spare[4];
};

#endif /* __s390x__ */
#endif
