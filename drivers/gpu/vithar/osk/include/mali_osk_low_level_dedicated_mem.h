/*
 *
 * (C) COPYRIGHT 2008-2010 ARM Limited. All rights reserved.
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
 * Implementation of the dedicated memory allocator for the kernel device driver
 */

#ifndef _OSK_LOW_LEVEL_DEDICATED_MEM_H_
#define _OSK_LOW_LEVEL_DEDICATED_MEM_H_

#ifdef __KERNEL__
#include <linux/io.h>
#endif /* __KERNEL__ */

struct oskp_phy_dedicated_allocator
{
	/* lock to protect the free map management */
	osk_mutex        lock;
	
	osk_phy_addr     base;
	u32              num_pages;
	u32              free_pages;

	unsigned long *  free_map;
};

OSK_STATIC_INLINE osk_error oskp_phy_dedicated_allocator_init(oskp_phy_dedicated_allocator * const allocator,
                                                              osk_phy_addr mem, u32 nr_pages, const char* name)
{
	osk_error error;

	OSK_ASSERT(allocator);
	OSK_ASSERT(nr_pages > 0);
	/* Assert if not page aligned */
	OSK_ASSERT( 0 == (mem & (OSK_PAGE_SIZE-1)) );

	if (!mem)
	{
		/* no address to manage specified */
		return OSK_ERR_FAIL;
	}
	else
	{
		u32 i;

		/* try to obtain dedicated memory */
		if(oskp_phy_dedicated_allocator_request_memory(mem, nr_pages, name) != OSK_ERR_NONE)
		{
			/* requested memory not available */
			return OSK_ERR_FAIL;
		}

		allocator->base = mem;
		allocator->num_pages  = nr_pages;
		allocator->free_pages = allocator->num_pages;

		error = osk_mutex_init(&allocator->lock, OSK_LOCK_ORDER_LAST );
		if (OSK_ERR_NONE != error)
		{
			return OSK_ERR_FAIL;
		}

		allocator->free_map = osk_calloc(sizeof(unsigned long) * ((nr_pages + OSK_BITS_PER_LONG - 1) / OSK_BITS_PER_LONG));
		if (NULL == allocator->free_map)
		{
			osk_mutex_term(&allocator->lock);
			return OSK_ERR_ALLOC;
		}

		/* correct for nr_pages not being a multiple of OSK_BITS_PER_LONG */
		for (i = nr_pages; i < ((nr_pages + OSK_BITS_PER_LONG - 1) & ~(OSK_BITS_PER_LONG-1)); i++)
		{
			osk_bitarray_set_bit(i, allocator->free_map);
		}

		return OSK_ERR_NONE;
	}
}

OSK_STATIC_INLINE void oskp_phy_dedicated_allocator_term(oskp_phy_dedicated_allocator *allocator)
{
	OSK_ASSERT(allocator);
	OSK_ASSERT(allocator->free_map);
	oskp_phy_dedicated_allocator_release_memory(allocator->base, allocator->num_pages);
	osk_free(allocator->free_map);
	osk_mutex_term(&allocator->lock);
}

OSK_STATIC_INLINE u32 oskp_phy_dedicated_pages_alloc(oskp_phy_dedicated_allocator *allocator,
                                                           u32 nr_pages, osk_phy_addr *pages)
{
	u32 pages_allocated;

	OSK_ASSERT(pages);
	OSK_ASSERT(allocator);
	OSK_ASSERT(allocator->free_map);

	osk_mutex_lock(&allocator->lock);

	for (pages_allocated = 0; pages_allocated < OSK_MIN(nr_pages, allocator->free_pages); pages_allocated++)
	{
		u32 pfn;
		void * mapping;

		pfn = osk_bitarray_find_first_zero_bit(allocator->free_map, allocator->num_pages);
		/* As the free_pages test passed ffz should never fail */
		OSK_ASSERT(pfn != allocator->num_pages);

		/* mark as allocated */
		osk_bitarray_set_bit(pfn, allocator->free_map);

		/* find phys addr of the page */
		pages[pages_allocated] = allocator->base + (pfn << OSK_PAGE_SHIFT);

#ifdef __KERNEL__
		/* zero the page */
		if(OSK_SIMULATE_FAILURE(OSK_OSK))
		{
			mapping = NULL;
		}
		else
		{
			mapping = ioremap_wc(pages[pages_allocated], SZ_4K);
		}
#else
		mapping = osk_kmap(pages[pages_allocated]);
#endif /* __KERNEL__ */

		if (NULL == mapping)
		{
			/* roll back */
			for (pages_allocated++; pages_allocated > 0; pages_allocated--)
			{
				pfn = (pages[pages_allocated-1] - allocator->base) >> OSK_PAGE_SHIFT;
				osk_bitarray_clear_bit(pfn, allocator->free_map);
			}
			break;
		}

		OSK_MEMSET(mapping, 0x00, OSK_PAGE_SIZE);

		osk_sync_to_memory(pages[pages_allocated], mapping, OSK_PAGE_SIZE);
#ifdef __KERNEL__
		iounmap(mapping);
#else
		osk_kunmap(pages[pages_allocated], mapping);
#endif /* __KERNEL__ */
	}

	allocator->free_pages -= pages_allocated;
	osk_mutex_unlock(&allocator->lock);

	return pages_allocated;
}

OSK_STATIC_INLINE void oskp_phy_dedicated_pages_free(oskp_phy_dedicated_allocator *allocator,
                                                     u32 nr_pages, osk_phy_addr *pages)
{
	u32 i;

	OSK_ASSERT(pages);
	OSK_ASSERT(allocator);
	OSK_ASSERT(allocator->free_map);

	osk_mutex_lock(&allocator->lock);

	for (i = 0; i < nr_pages; i++)
	{
		if (0 != pages[i])
		{
			u32 pfn;

			OSK_ASSERT(pages[i] >= allocator->base);
			OSK_ASSERT(pages[i] < allocator->base + (allocator->num_pages << OSK_PAGE_SHIFT));
		   
			pfn = (pages[i] - allocator->base) >> OSK_PAGE_SHIFT;
			osk_bitarray_clear_bit(pfn, allocator->free_map);

			allocator->free_pages++;

			pages[i] = 0;
		}
	}

	osk_mutex_unlock(&allocator->lock);
}

#endif /* _OSK_LOW_LEVEL_DEDICATED_MEM_H_ */
