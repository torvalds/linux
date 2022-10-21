/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

/*
 * This file defines the locations of the fixed mappings on parisc.
 *
 * All of the values in this file are machine virtual addresses.
 *
 * All of the values in this file must be <4GB (because of assembly
 * loading restrictions).  If you place this region anywhere above
 * __PAGE_OFFSET, you must adjust the memory map accordingly
 */

/*
 * The tmpalias region is used in kernel space to copy/clear/flush data
 * from pages congruently mapped with user space. It is comprised of
 * a pair regions. The size of these regions is determined by the largest
 * cache aliasing boundary for machines that support equivalent aliasing.
 *
 * The c3750 with PA8700 processor returns an alias value of 11. This
 * indicates that it has an alias boundary of 4 MB. It also supports
 * non-equivalent aliasing without a performance penalty.
 *
 * Machines with PA8800/PA8900 processors return an alias value of 0.
 * This indicates the alias boundary is unknown and may be larger than
 * 16 MB. Non-equivalent aliasing is not supported.
 *
 * Here we assume the maximum alias boundary is 4 MB.
 */
#define TMPALIAS_SIZE_BITS	22	/* 4 MB */
#define TMPALIAS_MAP_START	((__PAGE_OFFSET) - (2 << TMPALIAS_SIZE_BITS))

#define FIXMAP_SIZE		(FIX_BITMAP_COUNT << PAGE_SHIFT)
#define FIXMAP_START		(TMPALIAS_MAP_START - FIXMAP_SIZE)
/* This is the kernel area for all maps (vmalloc, dma etc.)  most
 * usually, it extends up to TMPALIAS_MAP_START.  Virtual addresses
 * 0..GATEWAY_PAGE_SIZE are reserved for the gateway page */
#define KERNEL_MAP_START	(GATEWAY_PAGE_SIZE)
#define KERNEL_MAP_END		(FIXMAP_START)

#ifndef __ASSEMBLY__


enum fixed_addresses {
	/* Support writing RO kernel text via kprobes, jump labels, etc. */
	FIX_TEXT_POKE0,
	FIX_TEXT_KEXEC,
	FIX_BITMAP_COUNT
};

extern void *parisc_vmalloc_start;
#define PCXL_DMA_MAP_SIZE	(8*1024*1024)
#define VMALLOC_START		((unsigned long)parisc_vmalloc_start)
#define VMALLOC_END		(KERNEL_MAP_END)

#define __fix_to_virt(_x) (FIXMAP_START + ((_x) << PAGE_SHIFT))

void set_fixmap(enum fixed_addresses idx, phys_addr_t phys);
void clear_fixmap(enum fixed_addresses idx);

#endif /*__ASSEMBLY__*/

#endif /*_ASM_FIXMAP_H*/
