/*
 * Memory layout definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef _ASM_HEXAGON_MEM_LAYOUT_H
#define _ASM_HEXAGON_MEM_LAYOUT_H

#include <linux/const.h>

/*
 * Have to do this for ginormous numbers, else they get printed as
 * negative numbers, which the linker no likey when you try to
 * assign it to the location counter.
 */

#define PAGE_OFFSET			_AC(0xc0000000, UL)

/*
 * Compiling for a platform that needs a crazy physical offset
 * (like if the memory starts at 1GB and up) means we need
 * an actual PHYS_OFFSET.  Should be set up in head.S.
 */

#ifdef CONFIG_HEXAGON_PHYS_OFFSET
#ifndef __ASSEMBLY__
extern unsigned long	__phys_offset;
#endif
#define PHYS_OFFSET	__phys_offset
#endif

#ifndef PHYS_OFFSET
#define PHYS_OFFSET	0
#endif

#define PHYS_PFN_OFFSET	(PHYS_OFFSET >> PAGE_SHIFT)
#define ARCH_PFN_OFFSET	PHYS_PFN_OFFSET

#define TASK_SIZE			(PAGE_OFFSET)

/*  not sure how these are used yet  */
#define STACK_TOP			TASK_SIZE
#define STACK_TOP_MAX			TASK_SIZE

#ifndef __ASSEMBLY__
enum fixed_addresses {
	FIX_KMAP_BEGIN,
	FIX_KMAP_END,  /*  check for per-cpuism  */
	__end_of_fixed_addresses
};

#define MIN_KERNEL_SEG (PAGE_OFFSET >> PGDIR_SHIFT)   /* L1 shift is 22 bits */
extern int max_kernel_seg;

/*
 * Start of vmalloc virtual address space for kernel;
 * supposed to be based on the amount of physical memory available
 */

#define VMALLOC_START ((unsigned long) __va(high_memory + VMALLOC_OFFSET))

/* Gap between physical ram and vmalloc space for guard purposes. */
#define VMALLOC_OFFSET PAGE_SIZE

/*
 * Create the space between VMALLOC_START and FIXADDR_TOP backwards
 * from the ... "top".
 *
 * Permanent IO mappings will live at 0xfexx_xxxx
 * Hypervisor occupies the last 16MB page at 0xffxxxxxx
 */

#define FIXADDR_TOP     0xfe000000
#define FIXADDR_SIZE    (__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START   (FIXADDR_TOP - FIXADDR_SIZE)

/*
 * "permanent kernel mappings", defined as long-lasting mappings of
 * high-memory page frames into the kernel address space.
 */

#define LAST_PKMAP	PTRS_PER_PTE
#define LAST_PKMAP_MASK	(LAST_PKMAP - 1)
#define PKMAP_NR(virt)	((virt - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)	(PKMAP_BASE + ((nr) << PAGE_SHIFT))

/*
 * To the "left" of the fixed map space is the kmap space
 *
 * "Permanent Kernel Mappings"; fancy (or less fancy) PTE table
 * that looks like it's actually walked.
 * Need to check the alignment/shift usage; some archs use
 * PMD_MASK on this value
 */
#define PKMAP_BASE (FIXADDR_START-PAGE_SIZE*LAST_PKMAP)

/*
 * 2 pages of guard gap between where vmalloc area ends
 * and pkmap_base begins.
 */
#define VMALLOC_END (PKMAP_BASE-PAGE_SIZE*2)
#endif /*  !__ASSEMBLY__  */


#endif /* _ASM_HEXAGON_MEM_LAYOUT_H */
