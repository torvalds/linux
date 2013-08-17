/*
 *
 * (C) COPYRIGHT 2008-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_ARCH_LOW_LEVEL_MEM_H_
#define _OSK_ARCH_LOW_LEVEL_MEM_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>

OSK_STATIC_INLINE osk_error oskp_phy_dedicated_allocator_request_memory(osk_phy_addr mem,u32 nr_pages, const char* name)
{
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return OSK_ERR_FAIL;
	}

	if (NULL != request_mem_region(mem, nr_pages << OSK_PAGE_SHIFT , name))
	{
		return OSK_ERR_NONE;
	}
	return OSK_ERR_FAIL;
}

OSK_STATIC_INLINE void oskp_phy_dedicated_allocator_release_memory(osk_phy_addr mem,u32 nr_pages)
{
	release_mem_region(mem, nr_pages << OSK_PAGE_SHIFT);
}


static inline void *osk_kmap(osk_phy_addr page)
{
	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return NULL;
	}

	return kmap(pfn_to_page(PFN_DOWN(page)));
}

static inline void osk_kunmap(osk_phy_addr page, void * mapping)
{
	kunmap(pfn_to_page(PFN_DOWN(page)));
}

static inline void *osk_kmap_atomic(osk_phy_addr page)
{
	/**
	 * Note: kmap_atomic should never fail and so OSK_SIMULATE_FAILURE is not
	 * included for this function call.
	 */
	return kmap_atomic(pfn_to_page(PFN_DOWN(page)));
}

static inline void osk_kunmap_atomic(osk_phy_addr page, void *mapping)
{
	kunmap_atomic(mapping);
}

static inline void osk_sync_to_memory(osk_phy_addr paddr, osk_virt_addr vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	dmac_flush_range(vaddr, vaddr+sz-1);
	outer_flush_range(paddr, paddr+sz-1);
#elif defined(CONFIG_X86)
	struct scatterlist scl = {0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz,
			paddr & (PAGE_SIZE -1 ));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_TO_DEVICE);
	mb(); /* for outer_sync (if needed) */
#else
#error Implement cache maintenance for your architecture here
#endif
}

static inline void osk_sync_to_cpu(osk_phy_addr paddr, osk_virt_addr vaddr, size_t sz)
{
#ifdef CONFIG_ARM
	dmac_flush_range(vaddr, vaddr+sz-1);
	outer_flush_range(paddr, paddr+sz-1);
#elif defined(CONFIG_X86)
	struct scatterlist scl = {0, };
	sg_set_page(&scl, pfn_to_page(PFN_DOWN(paddr)), sz,
			paddr & (PAGE_SIZE -1 ));
	dma_sync_sg_for_cpu(NULL, &scl, 1, DMA_FROM_DEVICE);
#else
#error Implement cache maintenance for your architecture here
#endif
}

#endif /* _OSK_ARCH_LOW_LEVEL_MEM_H_ */
