/* arch/arm/mach-msm/memory.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include "memory_ll.h"
#include <asm/cacheflush.h>
#if defined(CONFIG_MSM_NPA_REMOTE)
#include "npa_remote.h"
#include <linux/completion.h>
#include <linux/err.h>
#endif

int arch_io_remap_pfn_range(struct vm_area_struct *vma, unsigned long addr,
			    unsigned long pfn, unsigned long size, pgprot_t prot)
{
	unsigned long pfn_addr = pfn << PAGE_SHIFT;
/*
	if ((pfn_addr >= 0x88000000) && (pfn_addr < 0xD0000000)) {
		prot = pgprot_device(prot);
		printk("remapping device %lx\n", prot);
	}
*/
	panic("Memory remap PFN stuff not done\n");
	return remap_pfn_range(vma, addr, pfn, size, prot);
}

void *zero_page_strongly_ordered;

static void map_zero_page_strongly_ordered(void)
{
	if (zero_page_strongly_ordered)
		return;
/*
	zero_page_strongly_ordered =
		ioremap_strongly_ordered(page_to_pfn(empty_zero_page)
		<< PAGE_SHIFT, PAGE_SIZE);
*/
	panic("Strongly ordered memory functions not implemented\n");
}

void write_to_strongly_ordered_memory(void)
{
	map_zero_page_strongly_ordered();
	*(int *)zero_page_strongly_ordered = 0;
}
EXPORT_SYMBOL(write_to_strongly_ordered_memory);

void flush_axi_bus_buffer(void)
{
	__asm__ __volatile__ ("mcr p15, 0, %0, c7, c10, 5" \
				    : : "r" (0) : "memory");
	write_to_strongly_ordered_memory();
}

#define CACHE_LINE_SIZE 32

/* These cache related routines make the assumption that the associated
 * physical memory is contiguous. They will operate on all (L1
 * and L2 if present) caches.
 */
void clean_and_invalidate_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	unsigned long vaddr;

	for (vaddr = vstart; vaddr < vstart + length; vaddr += CACHE_LINE_SIZE)
		asm ("mcr p15, 0, %0, c7, c14, 1" : : "r" (vaddr));
#ifdef CONFIG_OUTER_CACHE
	outer_flush_range(pstart, pstart + length);
#endif
	asm ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));
	asm ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));

	flush_axi_bus_buffer();
}

void clean_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	unsigned long vaddr;

	for (vaddr = vstart; vaddr < vstart + length; vaddr += CACHE_LINE_SIZE)
		asm ("mcr p15, 0, %0, c7, c10, 1" : : "r" (vaddr));
#ifdef CONFIG_OUTER_CACHE
	outer_clean_range(pstart, pstart + length);
#endif
	asm ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));
	asm ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));

	flush_axi_bus_buffer();
}

void invalidate_caches(unsigned long vstart,
	unsigned long length, unsigned long pstart)
{
	unsigned long vaddr;

	for (vaddr = vstart; vaddr < vstart + length; vaddr += CACHE_LINE_SIZE)
		asm ("mcr p15, 0, %0, c7, c6, 1" : : "r" (vaddr));
#ifdef CONFIG_OUTER_CACHE
	outer_inv_range(pstart, pstart + length);
#endif
	asm ("mcr p15, 0, %0, c7, c10, 4" : : "r" (0));
	asm ("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));

	flush_axi_bus_buffer();
}

void *alloc_bootmem_aligned(unsigned long size, unsigned long alignment)
{
	void *unused_addr = NULL;
	unsigned long addr, tmp_size, unused_size;

	/* Allocate maximum size needed, see where it ends up.
	 * Then free it -- in this path there are no other allocators
	 * so we can depend on getting the same address back
	 * when we allocate a smaller piece that is aligned
	 * at the end (if necessary) and the piece we really want,
	 * then free the unused first piece.
	 */

	tmp_size = size + alignment - PAGE_SIZE;
	addr = (unsigned long)alloc_bootmem(tmp_size);
	free_bootmem(__pa(addr), tmp_size);

	unused_size = alignment - (addr % alignment);
	if (unused_size)
		unused_addr = alloc_bootmem(unused_size);

	addr = (unsigned long)alloc_bootmem(size);
	if (unused_size)
		free_bootmem(__pa(unused_addr), unused_size);

	return (void *)addr;
}

#if defined(CONFIG_MSM_NPA_REMOTE)
struct npa_client *npa_memory_client;
#endif

static int change_memory_power_state(unsigned long start_pfn,
	unsigned long nr_pages, int state)
{
#if defined(CONFIG_MSM_NPA_REMOTE)
	static atomic_t node_created_flag = ATOMIC_INIT(1);
#else
	unsigned long start;
	unsigned long size;
	unsigned long virtual;
#endif
	int rc = 0;

#if defined(CONFIG_MSM_NPA_REMOTE)
	if (atomic_dec_and_test(&node_created_flag)) {
		/* Create NPA 'required' client. */
		npa_memory_client = npa_create_sync_client(NPA_MEMORY_NODE_NAME,
			"memory node", NPA_CLIENT_REQUIRED);
		if (IS_ERR(npa_memory_client)) {
			rc = PTR_ERR(npa_memory_client);
			return rc;
		}
	}

	rc = npa_issue_required_request(npa_memory_client, state);
#else
	if (state == MEMORY_DEEP_POWERDOWN) {
		/* simulate turning off memory by writing bit pattern into it */
		start = start_pfn << PAGE_SHIFT;
		size = nr_pages << PAGE_SHIFT;
		virtual = __phys_to_virt(start);
		memset((void *)virtual, 0x27, size);
	}
#endif
	return rc;
}

int platform_physical_remove_pages(unsigned long start_pfn,
	unsigned long nr_pages)
{
	return change_memory_power_state(start_pfn, nr_pages,
		MEMORY_DEEP_POWERDOWN);
}

int platform_physical_add_pages(unsigned long start_pfn,
	unsigned long nr_pages)
{
	return change_memory_power_state(start_pfn, nr_pages, MEMORY_ACTIVE);
}

int platform_physical_low_power_pages(unsigned long start_pfn,
	unsigned long nr_pages)
{
	return change_memory_power_state(start_pfn, nr_pages,
		MEMORY_SELF_REFRESH);
}
