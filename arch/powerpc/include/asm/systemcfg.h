/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _SYSTEMCFG_H
#define _SYSTEMCFG_H

/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
 */

#ifdef CONFIG_PPC64

/*
 * If the major version changes we are incompatible.
 * Minor version changes are a hint.
 */
#define SYSTEMCFG_MAJOR 1
#define SYSTEMCFG_MINOR 1

#include <linux/types.h>

struct systemcfg {
	__u8  eye_catcher[16];		/* Eyecatcher: SYSTEMCFG:PPC64	0x00 */
	struct {			/* Systemcfg version numbers	     */
		__u32 major;		/* Major number			0x10 */
		__u32 minor;		/* Minor number			0x14 */
	} version;

	/* Note about the platform flags: it now only contains the lpar
	 * bit. The actual platform number is dead and buried
	 */
	__u32 platform;			/* Platform flags		0x18 */
	__u32 processor;		/* Processor type		0x1C */
	__u64 processorCount;		/* # of physical processors	0x20 */
	__u64 physicalMemorySize;	/* Size of real memory(B)	0x28 */
	__u64 tb_orig_stamp;		/* (NU) Timebase at boot	0x30 */
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec		0x38 */
	__u64 tb_to_xs;			/* (NU) Inverse of TB to 2^20	0x40 */
	__u64 stamp_xsec;		/* (NU)				0x48 */
	__u64 tb_update_count;		/* (NU) Timebase atomicity ctr	0x50 */
	__u32 tz_minuteswest;		/* (NU) Min. west of Greenwich	0x58 */
	__u32 tz_dsttime;		/* (NU) Type of dst correction	0x5C */
	__u32 dcache_size;		/* L1 d-cache size		0x60 */
	__u32 dcache_line_size;		/* L1 d-cache line size		0x64 */
	__u32 icache_size;		/* L1 i-cache size		0x68 */
	__u32 icache_line_size;		/* L1 i-cache line size		0x6C */
};

extern struct systemcfg *systemcfg;

#endif /* CONFIG_PPC64 */
#endif /* _SYSTEMCFG_H */
