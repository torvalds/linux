/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _VDSO_DATAPAGE_H
#define _VDSO_DATAPAGE_H
#ifdef __KERNEL__

/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
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
#include <vdso/datapage.h>

#define SYSCALL_MAP_SIZE      ((NR_syscalls + 31) / 32)

/*
 * So here is the ppc64 backward compatible version
 */

#ifdef CONFIG_PPC64

struct vdso_arch_data {
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

	/* those additional ones don't have to be located anywhere
	 * special as they were not part of the original systemcfg
	 */
	__u32 dcache_block_size;		/* L1 d-cache block size     */
	__u32 icache_block_size;		/* L1 i-cache block size     */
	__u32 dcache_log_block_size;		/* L1 d-cache log block size */
	__u32 icache_log_block_size;		/* L1 i-cache log block size */
	__u32 syscall_map[SYSCALL_MAP_SIZE];	/* Map of syscalls  */
	__u32 compat_syscall_map[SYSCALL_MAP_SIZE];	/* Map of compat syscalls */

	struct vdso_data data[CS_BASES];
	struct vdso_rng_data rng_data;
};

#else /* CONFIG_PPC64 */

/*
 * And here is the simpler 32 bits version
 */
struct vdso_arch_data {
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec		0x38 */
	__u32 syscall_map[SYSCALL_MAP_SIZE]; /* Map of syscalls */
	__u32 compat_syscall_map[0];	/* No compat syscalls on PPC32 */
	struct vdso_data data[CS_BASES];
	struct vdso_rng_data rng_data;
};

#endif /* CONFIG_PPC64 */

extern struct vdso_arch_data *vdso_data;

#else /* __ASSEMBLY__ */

.macro get_datapage ptr
	bcl	20, 31, .+4
999:
	mflr	\ptr
	addis	\ptr, \ptr, (_vdso_datapage - 999b)@ha
	addi	\ptr, \ptr, (_vdso_datapage - 999b)@l
.endm

#include <asm/asm-offsets.h>
#include <asm/page.h>

.macro get_realdatapage ptr scratch
	get_datapage \ptr
#ifdef CONFIG_TIME_NS
	lwz	\scratch, VDSO_CLOCKMODE_OFFSET(\ptr)
	xoris	\scratch, \scratch, VDSO_CLOCKMODE_TIMENS@h
	xori	\scratch, \scratch, VDSO_CLOCKMODE_TIMENS@l
	cntlzw	\scratch, \scratch
	rlwinm	\scratch, \scratch, PAGE_SHIFT - 5, 1 << PAGE_SHIFT
	add	\ptr, \ptr, \scratch
#endif
.endm

#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _SYSTEMCFG_H */
