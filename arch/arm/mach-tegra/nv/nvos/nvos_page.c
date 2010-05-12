/*
 * arch/arm/mach-tegra/nvos/nvos_page.c
 *
 * Implementation of NvOsPage* APIs using the Linux page allocator
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include "nvcommon.h"
#include "nvos.h"
#include <linux/slab.h>

#if NVOS_TRACE || NV_DEBUG
#undef NvOsPageMap
#undef NvOsPageGetPage
#undef NvOsPageAddress
#undef NvOsPageUnmap
#undef NvOsPageAlloc
#undef NvOsPageFree
#undef NvOsPageLock
#undef NvOsPageMapIntoPtr
#endif

#define L_PTE_MT_INNER_WB       (0x05 << 2)     /* 0101 (armv6, armv7) */
#define pgprot_inner_writeback(prot) \
        __pgprot((pgprot_val(prot) & ~L_PTE_MT_MASK) | L_PTE_MT_INNER_WB)

#define nv_gfp_pool (GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN)

struct nvos_pagemap {
	void		*addr;
	unsigned int	nr_pages;
	struct page	*pages[1];
};

static void pagemap_flush_page(struct page *page)
{
#ifdef CONFIG_HIGHMEM
	void *km = NULL;

	if (!page_address(page)) {
		km = kmap(page);
		if (!km) {
			pr_err("unable to map high page\n");
			return;
		}
	}
#endif

	flush_dcache_page(page_address(page));
	outer_flush_range(page_to_phys(page), page_to_phys(page)+PAGE_SIZE);
	dsb();

#ifdef CONFIG_HIGHMEM
	if (km) kunmap(page);
#endif
}

static void nv_free_pages(struct nvos_pagemap *pm)
{
	unsigned int i;

	if (pm->addr) vm_unmap_ram(pm->addr, pm->nr_pages);

	for (i=0; i<pm->nr_pages; i++) {
		ClearPageReserved(pm->pages[i]);
		__free_page(pm->pages[i]);
	}
	kfree(pm);
}

static struct nvos_pagemap *nv_alloc_pages(unsigned int count,
	pgprot_t prot, bool contiguous, int create_mapping)
{
	struct nvos_pagemap *pm;
	size_t size;
	unsigned int i = 0;

	size = sizeof(struct nvos_pagemap) + sizeof(struct page *)*(count-1);
	pm = kzalloc(size, GFP_KERNEL);
	if (!pm)
		return NULL;

	if (count==1) contiguous = true;

	if (contiguous) {
		size_t order = get_order(count << PAGE_SHIFT);
		struct page *compound_page;
		compound_page = alloc_pages(nv_gfp_pool, order);
		if (!compound_page) goto fail;

		split_page(compound_page, order);
		for (i=0; i<count; i++)
			pm->pages[i] = nth_page(compound_page, i);

		for ( ; i < (1<<order); i++)
			__free_page(nth_page(compound_page, i));
		i = count;
	} else {
		for (i=0; i<count; i++) {
			pm->pages[i] = alloc_page(nv_gfp_pool);
			if (!pm->pages[i]) goto fail;
		}
	}

	if (create_mapping) {
		/* since the linear kernel mapping uses sections and super-
	 	 * sections rather than PTEs, it's not possible to overwrite
		 * it with the correct caching attributes, so use a local
		 * mapping */
		pm->addr = vm_map_ram(pm->pages, count, -1, prot);
		if (!pm->addr) {
			pr_err("nv_alloc_pages fail to vmap contiguous area\n");
			goto fail;
		}
	}

	pm->nr_pages = count;
	for (i=0; i<count; i++) {
		SetPageReserved(pm->pages[i]);
		pagemap_flush_page(pm->pages[i]);
	}

        return pm;

fail:
	while (i) __free_page(pm->pages[--i]);
	if (pm) kfree(pm);
	return NULL;
}

NvError NvOsPageMap(NvOsPageAllocHandle desc, size_t offs,
	size_t size, void **ptr)
{
	struct nvos_pagemap *pm = (struct nvos_pagemap *)desc;
	if (!desc || !ptr || !size)
		return NvError_BadParameter;

	if (pm->addr) *ptr = (void*)((unsigned long)pm->addr + offs);
	else *ptr = NULL;

	return (*ptr) ? NvSuccess : NvError_MemoryMapFailed;
}

struct page *NvOsPageGetPage(NvOsPageAllocHandle desc, size_t offs)
{
	struct nvos_pagemap *pm = (struct nvos_pagemap *)desc;
	if (!pm) return NULL;

	offs >>= PAGE_SHIFT;
	return (likely(offs<pm->nr_pages)) ? pm->pages[offs] : NULL;
}

NvOsPhysAddr NvOsPageAddress(NvOsPageAllocHandle desc, size_t offs)
{
	struct nvos_pagemap *pm = (struct nvos_pagemap *)desc;
	size_t index;

	if (unlikely(!pm)) return (NvOsPhysAddr)0;

	index = offs >> PAGE_SHIFT;
	offs &= (PAGE_SIZE - 1);

	return (NvOsPhysAddr)(page_to_phys(pm->pages[index]) + offs);
}


void NvOsPageUnmap(NvOsPageAllocHandle desc, void *ptr, size_t size)
{
	return;
}

NvError NvOsPageAlloc(size_t size, NvOsMemAttribute attrib,
	NvOsPageFlags flags, NvU32 protect, NvOsPageAllocHandle *desc)
{
	struct nvos_pagemap *pm;
	pgprot_t prot = pgprot_kernel;
	size += PAGE_SIZE-1;
	size >>= PAGE_SHIFT;

	/* writeback is implemented as inner-cacheable only, since these
	 * allocators are only used to allocate buffers for DMA-driven
	 * clients, and the cost of L2 maintenance makes outer cacheability
	 * a net performance loss more often than not */
	if (attrib == NvOsMemAttribute_WriteBack)
		prot = pgprot_inner_writeback(prot);
	else
		prot = pgprot_writecombine(prot);

	pm = nv_alloc_pages(size, prot, (flags==NvOsPageFlags_Contiguous), 1);

	if (!pm) return NvError_InsufficientMemory;

	*desc = (NvOsPageAllocHandle)pm;
	return NvSuccess;
}

void NvOsPageFree(NvOsPageAllocHandle desc)
{
	struct nvos_pagemap *pm = (struct nvos_pagemap *)desc;

	if (pm) nv_free_pages(pm);
}


NvError NvOsPageLock(void *ptr, size_t size, NvU32 protect,
	NvOsPageAllocHandle *descriptor)
{
	return NvError_NotImplemented;
}

NvError NvOsPageMapIntoPtr(NvOsPageAllocHandle desc, void *ptr,
    size_t offset, size_t size)
{
	return NvError_NotImplemented;
}
