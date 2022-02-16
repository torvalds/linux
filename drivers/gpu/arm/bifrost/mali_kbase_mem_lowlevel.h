/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2012-2014, 2016-2018, 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KBASE_MEM_LOWLEVEL_H
#define _KBASE_MEM_LOWLEVEL_H

#ifndef _KBASE_H_
#error "Don't include this file directly, use mali_kbase.h instead"
#endif

#include <linux/dma-mapping.h>

/* Flags for kbase_phy_allocator_pages_alloc */
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

#define NUM_4K_PAGES_IN_2MB_PAGE (SZ_2M / SZ_4K)

/*
 * Note: if macro for converting physical address to page is not defined
 * in the kernel itself, it is defined hereby. This is to avoid build errors
 * which are reported during builds for some architectures.
 */
#ifndef phys_to_page
#define phys_to_page(phys)	(pfn_to_page((phys) >> PAGE_SHIFT))
#endif

/**
 * as_phys_addr_t - Retrieve the physical address from tagged address by
 *                  masking the lower order 12 bits.
 * @t: tagged address to be translated.
 *
 * Return: physical address corresponding to tagged address.
 */
static inline phys_addr_t as_phys_addr_t(struct tagged_addr t)
{
	return t.tagged_addr & PAGE_MASK;
}

/**
 * as_page - Retrieve the struct page from a tagged address
 * @t: tagged address to be translated.
 *
 * Return: pointer to struct page corresponding to tagged address.
 */
static inline struct page *as_page(struct tagged_addr t)
{
	return phys_to_page(as_phys_addr_t(t));
}

/**
 * as_tagged - Convert the physical address to tagged address type though
 *             there is no tag info present, the lower order 12 bits will be 0
 * @phys: physical address to be converted to tagged type
 *
 * This is used for 4KB physical pages allocated by the Driver or imported pages
 * and is needed as physical pages tracking object stores the reference for
 * physical pages using tagged address type in lieu of the type generally used
 * for physical addresses.
 *
 * Return: address of tagged address type.
 */
static inline struct tagged_addr as_tagged(phys_addr_t phys)
{
	struct tagged_addr t;

	t.tagged_addr = phys & PAGE_MASK;
	return t;
}

/**
 * as_tagged_tag - Form the tagged address by storing the tag or metadata in the
 *                 lower order 12 bits of physial address
 * @phys: physical address to be converted to tagged address
 * @tag:  tag to be stored along with the physical address.
 *
 * The tag info is used while freeing up the pages
 *
 * Return: tagged address storing physical address & tag.
 */
static inline struct tagged_addr as_tagged_tag(phys_addr_t phys, int tag)
{
	struct tagged_addr t;

	t.tagged_addr = (phys & PAGE_MASK) | (tag & ~PAGE_MASK);
	return t;
}

/**
 * is_huge - Check if the physical page is one of the 512 4KB pages of the
 *           large page which was not split to be used partially
 * @t: tagged address storing the tag in the lower order bits.
 *
 * Return: true if page belongs to large page, or false
 */
static inline bool is_huge(struct tagged_addr t)
{
	return t.tagged_addr & HUGE_PAGE;
}

/**
 * is_huge_head - Check if the physical page is the first 4KB page of the
 *                512 4KB pages within a large page which was not split
 *                to be used partially
 * @t: tagged address storing the tag in the lower order bits.
 *
 * Return: true if page is the first page of a large page, or false
 */
static inline bool is_huge_head(struct tagged_addr t)
{
	int mask = HUGE_HEAD | HUGE_PAGE;

	return mask == (t.tagged_addr & mask);
}

/**
 * is_partial - Check if the physical page is one of the 512 pages of the
 *              large page which was split in 4KB pages to be used
 *              partially for allocations >= 2 MB in size.
 * @t: tagged address storing the tag in the lower order bits.
 *
 * Return: true if page was taken from large page used partially, or false
 */
static inline bool is_partial(struct tagged_addr t)
{
	return t.tagged_addr & FROM_PARTIAL;
}

/**
 * index_in_large_page() - Get index of a 4KB page within a 2MB page which
 *                         wasn't split to be used partially.
 *
 * @t:  Tagged physical address of the physical 4KB page that lies within
 *      the large (or 2 MB) physical page.
 *
 * Return: Index of the 4KB page within a 2MB page
 */
static inline unsigned int index_in_large_page(struct tagged_addr t)
{
	WARN_ON(!is_huge(t));

	return (PFN_DOWN(as_phys_addr_t(t)) & (NUM_4K_PAGES_IN_2MB_PAGE - 1));
}

#endif /* _KBASE_LOWLEVEL_H */
