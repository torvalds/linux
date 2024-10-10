/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _VDSO_DATAPAGE_H
#define _VDSO_DATAPAGE_H
#ifdef __KERNEL__

/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
 */

#ifndef __ASSEMBLY__

#include <linux/unistd.h>
#include <linux/time.h>
#include <vdso/datapage.h>

#define SYSCALL_MAP_SIZE      ((NR_syscalls + 31) / 32)

#ifdef CONFIG_PPC64

struct vdso_arch_data {
	__u64 tb_ticks_per_sec;			/* Timebase tics / sec */
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

struct vdso_arch_data {
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec */
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
