/*
 *  include/asm-s390/statfs.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/statfs.h"
 */

#ifndef _S390_STATFS_H
#define _S390_STATFS_H

#ifndef __s390x__
#include <asm-generic/statfs.h>
#else

#ifndef __KERNEL_STRICT_NAMES

#include <linux/types.h>

typedef __kernel_fsid_t	fsid_t;

#endif

/*
 * This is ugly -- we're already 64-bit clean, so just duplicate the 
 * definitions.
 */
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
	int  f_spare[5];
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
	int  f_spare[5];
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
	__u32 f_spare[5];
};

#endif /* __s390x__ */
#endif
