#ifndef _VDSO_DATAPAGE_H
#define _VDSO_DATAPAGE_H
#ifdef __KERNEL__

/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


/*
 * Note about this structure:
 *
 * This structure was historically called systemcfg and exposed to
 * userland via /proc/ppc64/systemcfg. Unfortunately, this became an
 * ABI issue as some proprietary software started relying on being able
 * to mmap() it, thus we have to keep the base layout at least for a
 * few kernel versions.
 *
 * However, since ppc32 doesn't suffer from this backward handicap,
 * a simpler version of the data structure is used there with only the
 * fields actually used by the vDSO.
 *
 */

/*
 * If the major version changes we are incompatible.
 * Minor version changes are a hint.
 */
#define SYSTEMCFG_MAJOR 1
#define SYSTEMCFG_MINOR 1

#ifndef __ASSEMBLY__

#include <linux/unistd.h>
#include <linux/time.h>

#define SYSCALL_MAP_SIZE      ((__NR_syscalls + 31) / 32)

/*
 * So here is the ppc64 backward compatible version
 */

#ifdef CONFIG_PPC64

struct vdso_data {
	__u8  eye_catcher[16];		/* Eyecatcher: SYSTEMCFG:PPC64	0x00 */
	struct {			/* Systemcfg version numbers	     */
		__u32 major;		/* Major number			0x10 */
		__u32 minor;		/* Minor number			0x14 */
	} version;

	/* Note about the platform flags: it now only contains the lpar
	 * bit. The actual platform number is dead and burried
	 */
	__u32 platform;			/* Platform flags		0x18 */
	__u32 processor;		/* Processor type		0x1C */
	__u64 processorCount;		/* # of physical processors	0x20 */
	__u64 physicalMemorySize;	/* Size of real memory(B)	0x28 */
	__u64 tb_orig_stamp;		/* Timebase at boot		0x30 */
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec		0x38 */
	__u64 tb_to_xs;			/* Inverse of TB to 2^20	0x40 */
	__u64 stamp_xsec;		/*				0x48 */
	__u64 tb_update_count;		/* Timebase atomicity ctr	0x50 */
	__u32 tz_minuteswest;		/* Minutes west of Greenwich	0x58 */
	__u32 tz_dsttime;		/* Type of dst correction	0x5C */
	__u32 dcache_size;		/* L1 d-cache size		0x60 */
	__u32 dcache_line_size;		/* L1 d-cache line size		0x64 */
	__u32 icache_size;		/* L1 i-cache size		0x68 */
	__u32 icache_line_size;		/* L1 i-cache line size		0x6C */

	/* those additional ones don't have to be located anywhere
	 * special as they were not part of the original systemcfg
	 */
	__u32 dcache_block_size;		/* L1 d-cache block size     */
	__u32 icache_block_size;		/* L1 i-cache block size     */
	__u32 dcache_log_block_size;		/* L1 d-cache log block size */
	__u32 icache_log_block_size;		/* L1 i-cache log block size */
	__s32 wtom_clock_sec;			/* Wall to monotonic clock */
	__s32 wtom_clock_nsec;
	struct timespec stamp_xtime;	/* xtime as at tb_orig_stamp */
	__u32 stamp_sec_fraction;	/* fractional seconds of stamp_xtime */
   	__u32 syscall_map_64[SYSCALL_MAP_SIZE]; /* map of syscalls  */
   	__u32 syscall_map_32[SYSCALL_MAP_SIZE]; /* map of syscalls */
};

#else /* CONFIG_PPC64 */

/*
 * And here is the simpler 32 bits version
 */
struct vdso_data {
	__u64 tb_orig_stamp;		/* Timebase at boot		0x30 */
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec		0x38 */
	__u64 tb_to_xs;			/* Inverse of TB to 2^20	0x40 */
	__u64 stamp_xsec;		/*				0x48 */
	__u32 tb_update_count;		/* Timebase atomicity ctr	0x50 */
	__u32 tz_minuteswest;		/* Minutes west of Greenwich	0x58 */
	__u32 tz_dsttime;		/* Type of dst correction	0x5C */
	__s32 wtom_clock_sec;			/* Wall to monotonic clock */
	__s32 wtom_clock_nsec;
	struct timespec stamp_xtime;	/* xtime as at tb_orig_stamp */
	__u32 stamp_sec_fraction;	/* fractional seconds of stamp_xtime */
   	__u32 syscall_map_32[SYSCALL_MAP_SIZE]; /* map of syscalls */
	__u32 dcache_block_size;	/* L1 d-cache block size     */
	__u32 icache_block_size;	/* L1 i-cache block size     */
	__u32 dcache_log_block_size;	/* L1 d-cache log block size */
	__u32 icache_log_block_size;	/* L1 i-cache log block size */
};

#endif /* CONFIG_PPC64 */

#ifdef __KERNEL__
extern struct vdso_data *vdso_data;
#endif

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _SYSTEMCFG_H */
