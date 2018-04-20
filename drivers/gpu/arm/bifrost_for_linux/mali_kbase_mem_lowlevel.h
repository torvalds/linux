/*
 *
 * (C) COPYRIGHT 2012-2014,2017 ARM Limited. All rights reserved.
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

enum kbase_sync_type {
	KBASE_SYNC_TO_CPU,
	KBASE_SYNC_TO_DEVICE
};

struct tagged_addr { phys_addr_t tagged_addr; };

#define HUGE_PAGE    (1u << 0)
#define HUGE_HEAD    (1u << 1)
#define FROM_PARTIAL (1u << 2)

static inline phys_addr_t as_phys_addr_t(struct tagged_addr t)
{
	return t.tagged_addr & PAGE_MASK;
}

static inline struct tagged_addr as_tagged(phys_addr_t phys)
{
	struct tagged_addr t;

	t.tagged_addr = phys & PAGE_MASK;
	return t;
}

static inline struct tagged_addr as_tagged_tag(phys_addr_t phys, int tag)
{
	struct tagged_addr t;

	t.tagged_addr = (phys & PAGE_MASK) | (tag & ~PAGE_MASK);
	return t;
}

static inline bool is_huge(struct tagged_addr t)
{
	return t.tagged_addr & HUGE_PAGE;
}

static inline bool is_huge_head(struct tagged_addr t)
{
	int mask = HUGE_HEAD | HUGE_PAGE;

	return mask == (t.tagged_addr & mask);
}

static inline bool is_partial(struct tagged_addr t)
{
	return t.tagged_addr & FROM_PARTIAL;
}

#endif /* _KBASE_LOWLEVEL_H */
