/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hexagon VM page table entry definitions
 *
 * Copyright (c) 2010-2011,2013 The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_VM_MMU_H
#define _ASM_VM_MMU_H

/*
 * Shift, mask, and other constants for the Hexagon Virtual Machine
 * page tables.
 *
 * Virtual machine MMU allows first-level entries to either be
 * single-level lookup PTEs for very large pages, or PDEs pointing
 * to second-level PTEs for smaller pages. If PTE is single-level,
 * the least significant bits cannot be used as software bits to encode
 * virtual memory subsystem information about the page, and that state
 * must be maintained in some parallel data structure.
 */

/* S or Page Size field in PDE */
#define	__HVM_PDE_S		(0x7 << 0)
#define __HVM_PDE_S_4KB		0
#define __HVM_PDE_S_16KB	1
#define __HVM_PDE_S_64KB	2
#define __HVM_PDE_S_256KB	3
#define __HVM_PDE_S_1MB		4
#define __HVM_PDE_S_4MB		5
#define __HVM_PDE_S_16MB	6
#define __HVM_PDE_S_INVALID	7

/* Masks for L2 page table pointer, as function of page size */
#define __HVM_PDE_PTMASK_4KB	0xfffff000
#define __HVM_PDE_PTMASK_16KB	0xfffffc00
#define __HVM_PDE_PTMASK_64KB	0xffffff00
#define __HVM_PDE_PTMASK_256KB	0xffffffc0
#define __HVM_PDE_PTMASK_1MB	0xfffffff0

/*
 * Virtual Machine PTE Bits/Fields
 */
#define __HVM_PTE_T		(1<<4)
#define __HVM_PTE_U		(1<<5)
#define	__HVM_PTE_C		(0x7<<6)
#define __HVM_PTE_CVAL(pte)	(((pte) & __HVM_PTE_C) >> 6)
#define __HVM_PTE_R		(1<<9)
#define __HVM_PTE_W		(1<<10)
#define __HVM_PTE_X		(1<<11)

/*
 * Cache Attributes, to be shifted as necessary for virtual/physical PTEs
 */

#define __HEXAGON_C_WB		0x0	/* Write-back, no L2 */
#define	__HEXAGON_C_WT		0x1	/* Write-through, no L2 */
#define	__HEXAGON_C_UNC		0x6	/* Uncached memory */
#if CONFIG_HEXAGON_ARCH_VERSION >= 2
#define	__HEXAGON_C_DEV		0x4	/* Device register space */
#else
#define __HEXAGON_C_DEV		__HEXAGON_C_UNC
#endif
#define	__HEXAGON_C_WT_L2	0x5	/* Write-through, with L2 */
#define	__HEXAGON_C_WB_L2	0x7	/* Write-back, with L2 */

/*
 * This can be overridden, but we're defaulting to the most aggressive
 * cache policy, the better to find bugs sooner.
 */

#define	CACHE_DEFAULT	__HEXAGON_C_WB_L2

/* Masks for physical page address, as a function of page size */

#define __HVM_PTE_PGMASK_4KB	0xfffff000
#define __HVM_PTE_PGMASK_16KB	0xffffc000
#define __HVM_PTE_PGMASK_64KB	0xffff0000
#define __HVM_PTE_PGMASK_256KB	0xfffc0000
#define __HVM_PTE_PGMASK_1MB	0xfff00000

/* Masks for single-level large page lookups */

#define __HVM_PTE_PGMASK_4MB	0xffc00000
#define __HVM_PTE_PGMASK_16MB	0xff000000

/*
 * "Big kernel page mappings" (see vm_init_segtable.S)
 * are currently 16MB
 */

#define BIG_KERNEL_PAGE_SHIFT 24
#define BIG_KERNEL_PAGE_SIZE (1 << BIG_KERNEL_PAGE_SHIFT)



#endif /* _ASM_VM_MMU_H */
