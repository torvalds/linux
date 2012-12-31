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

struct oskp_phy_os_allocator
{
};

OSK_STATIC_INLINE osk_error oskp_phy_os_allocator_init(oskp_phy_os_allocator * const allocator,
                                                       osk_phy_addr mem, u32 nr_pages)
{
	OSK_ASSERT(NULL != allocator);

	return OSK_ERR_NONE;
}

OSK_STATIC_INLINE void oskp_phy_os_allocator_term(oskp_phy_os_allocator *allocator)
{
	OSK_ASSERT(NULL != allocator);
	/* Nothing needed */
}

OSK_STATIC_INLINE u32 oskp_phy_os_pages_alloc(oskp_phy_os_allocator *allocator,
                                                    u32 nr_pages, osk_phy_addr *pages)
{
	int i;

	OSK_ASSERT(NULL != allocator);

	if(OSK_SIMULATE_FAILURE(OSK_OSK))
	{
		return 0;
	}

	for (i = 0; i < nr_pages; i++)
	{
		struct page *p;
		void * mp;

		p = alloc_page(__GFP_IO |
		               __GFP_FS |
		               __GFP_COLD |
		               __GFP_NOWARN |
		               __GFP_NORETRY |
		               __GFP_NOMEMALLOC |
		               __GFP_HIGHMEM |
		               __GFP_HARDWALL);
		if (NULL == p)
		{
			break;
		}

		mp = kmap(p);
		if (NULL == mp)
		{
			__free_page(p);
			break;
		}

		memset(mp, 0x00, PAGE_SIZE); /* instead of __GFP_ZERO, so we can do cache maintenance */
		osk_sync_to_memory(PFN_PHYS(page_to_pfn(p)), mp, PAGE_SIZE);
		kunmap(p);

		pages[i] = PFN_PHYS(page_to_pfn(p));
	}

	return i;
}

static inline void oskp_phy_os_pages_free(oskp_phy_os_allocator *allocator,
                                          u32 nr_pages, osk_phy_addr *pages)
{
	int i;

	OSK_ASSERT(NULL != allocator);

	for (i = 0; i < nr_pages; i++)
	{
		if (0 != pages[i])
		{
			__free_page(pfn_to_page(PFN_DOWN(pages[i])));
			pages[i] = (osk_phy_addr)0;
		}
	}
}


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

#if MALI_DEBUG
void osk_kmap_debug(int slot);
void osk_kunmap_debug(int slot);
#endif

static inline void *osk_kmap_atomic(osk_phy_addr page, osk_kmap_slot slot)
{
	/**
	 * Note: kmap_atomic should never fail and so OSK_SIMULATE_FAILURE is not
	 * included for this function call.
	 */
	OSK_ASSERT((slot >= OSK_KMAP_SLOT_0) && (slot <= OSK_KMAP_SLOT_1));
	OSK_DEBUG_CODE( osk_kmap_debug(slot) );

	preempt_disable();
	return kmap_atomic(pfn_to_page(PFN_DOWN(page)), KM_USER0+slot);
}

static inline void osk_kunmap_atomic(osk_phy_addr page, void *mapping, osk_kmap_slot slot)
{
	OSK_ASSERT((slot >= OSK_KMAP_SLOT_0) && (slot <= OSK_KMAP_SLOT_1));
	OSK_DEBUG_CODE( osk_kunmap_debug(slot) );

	kunmap_atomic(mapping, KM_USER0+slot);
	preempt_enable();
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
