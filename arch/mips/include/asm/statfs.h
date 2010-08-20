/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1999 by Ralf Baechle
 */
#ifndef _ASM_STATFS_H
#define _ASM_STATFS_H

#include <linux/posix_types.h>
#include <asm/sgidefs.h>

#ifndef __KERNEL_STRICT_NAMES

#include <linux/types.h>

typedef __kernel_fsid_t        fsid_t;

#endif

struct statfs {
	long		f_type;
#define f_fstyp f_type
	long		f_bsize;
	long		f_frsize;	/* Fragment size - unsupported */
	long		f_blocks;
	long		f_bfree;
	long		f_files;
	long		f_ffree;
	long		f_bavail;

	/* Linux specials */
	__kernel_fsid_t	f_fsid;
	long		f_namelen;
	long		f_flags;
	long		f_spare[5];
};

#if (_MIPS_SIM == _MIPS_SIM_ABI32) || (_MIPS_SIM == _MIPS_SIM_NABI32)

/*
 * Unlike the traditional version the LFAPI version has none of the ABI junk
 */
struct statfs64 {
	__u32	f_type;
	__u32	f_bsize;
	__u32	f_frsize;	/* Fragment size - unsupported */
	__u32	__pad;
	__u64	f_blocks;
	__u64	f_bfree;
	__u64	f_files;
	__u64	f_ffree;
	__u64	f_bavail;
	__kernel_fsid_t f_fsid;
	__u32	f_namelen;
	__u32	f_flags;
	__u32	f_spare[5];
};

#endif /* _MIPS_SIM == _MIPS_SIM_ABI32 */

#if _MIPS_SIM == _MIPS_SIM_ABI64

struct statfs64 {			/* Same as struct statfs */
	long		f_type;
	long		f_bsize;
	long		f_frsize;	/* Fragment size - unsupported */
	long		f_blocks;
	long		f_bfree;
	long		f_files;
	long		f_ffree;
	long		f_bavail;

	/* Linux specials */
	__kernel_fsid_t	f_fsid;
	long		f_namelen;
	long		f_flags;
	long		f_spare[5];
};

struct compat_statfs64 {
	__u32	f_type;
	__u32	f_bsize;
	__u32	f_frsize;	/* Fragment size - unsupported */
	__u32	__pad;
	__u64	f_blocks;
	__u64	f_bfree;
	__u64	f_files;
	__u64	f_ffree;
	__u64	f_bavail;
	__kernel_fsid_t f_fsid;
	__u32	f_namelen;
	__u32	f_flags;
	__u32	f_spare[5];
};

#endif /* _MIPS_SIM == _MIPS_SIM_ABI64 */

#endif /* _ASM_STATFS_H */
