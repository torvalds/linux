/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





#ifndef _KBASE_MEM_LOWLEVEL_H
#define _KBASE_MEM_LOWLEVEL_H

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

#include <linux/dma-mapping.h>

/**
 * @brief Flags for kbase_phy_allocator_pages_alloc
 */
#define KBASE_PHY_PAGES_FLAG_DEFAULT (0)	/** Default allocation flag */
#define KBASE_PHY_PAGES_FLAG_CLEAR   (1 << 0)	/** Clear the pages after allocation */
#define KBASE_PHY_PAGES_FLAG_POISON  (1 << 1)	/** Fill the memory with a poison value */

#define KBASE_PHY_PAGES_SUPPORTED_FLAGS (KBASE_PHY_PAGES_FLAG_DEFAULT|KBASE_PHY_PAGES_FLAG_CLEAR|KBASE_PHY_PAGES_FLAG_POISON)

#define KBASE_PHY_PAGES_POISON_VALUE  0xFD /** Value to fill the memory with when KBASE_PHY_PAGES_FLAG_POISON is set */

/**
 * A pointer to a cache synchronization function, either dma_sync_single_for_cpu
 * or dma_sync_single_for_device.
 */
typedef void (*kbase_sync_kmem_fn) (struct device *, dma_addr_t, size_t size,
				   enum dma_data_direction);


#endif				/* _KBASE_LOWLEVEL_H */
