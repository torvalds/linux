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
 * __PAGE_OFFSET, you must adjust the memory map accordingly */

/* The alias region is used in kernel space to do copy/clear to or
 * from areas congruently mapped with user space.  It is 8MB large
 * and must be 16MB aligned */
#define TMPALIAS_MAP_START	((__PAGE_OFFSET) - 16*1024*1024)

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
