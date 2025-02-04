/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 * Copyright (C) 2005 Benjamin Herrenschmidy <benh@kernel.crashing.org>,
 * 		      IBM Corp.
 */
#ifndef _ASM_POWERPC_VDSO_ARCH_DATA_H
#define _ASM_POWERPC_VDSO_ARCH_DATA_H

#include <linux/unistd.h>
#include <linux/types.h>

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
};

#else /* CONFIG_PPC64 */

struct vdso_arch_data {
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec */
	__u32 syscall_map[SYSCALL_MAP_SIZE]; /* Map of syscalls */
	__u32 compat_syscall_map[0];	/* No compat syscalls on PPC32 */
};

#endif /* CONFIG_PPC64 */

#endif /* _ASM_POWERPC_VDSO_ARCH_DATA_H */
