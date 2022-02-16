// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2021 ARM Limited. All rights reserved.
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

#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/protected_memory_allocator.h>

/* Size of a bitfield element in bytes */
#define BITFIELD_ELEM_SIZE sizeof(u64)

/* We can track whether or not 64 pages are currently allocated in a u64 */
#define PAGES_PER_BITFIELD_ELEM (BITFIELD_ELEM_SIZE * BITS_PER_BYTE)

/* Order 6 (ie, 64) corresponds to the number of pages held in a bitfield */
#define ORDER_OF_PAGES_PER_BITFIELD_ELEM 6

/**
 * struct simple_pma_device - Simple implementation of a protected memory
 *                            allocator device
 * @pma_dev: Protected memory allocator device pointer
 * @dev:     Device pointer
 * @allocated_pages_bitfield_arr: Status of all the physical memory pages within the
 *                                protected memory region, one bit per page
 * @rmem_base:      Base address of the reserved memory region
 * @rmem_size:      Size of the reserved memory region, in pages
 * @num_free_pages: Number of free pages in the memory region
 * @rmem_lock:      Lock to serialize the allocation and freeing of
 *                  physical pages from the protected memory region
 */
struct simple_pma_device {
	struct protected_memory_allocator_device pma_dev;
	struct device *dev;
	u64 *allocated_pages_bitfield_arr;
	phys_addr_t rmem_base;
	size_t rmem_size;
	size_t num_free_pages;
	spinlock_t rmem_lock;
};

/**
 * ALLOC_PAGES_BITFIELD_ARR_SIZE() - Number of elements in array
 *                                   'allocated_pages_bitfield_arr'
 * If the number of pages required does not divide exactly by
 * PAGES_PER_BITFIELD_ELEM, adds an extra page for the remainder.
 * @num_pages: number of pages
 */
#define ALLOC_PAGES_BITFIELD_ARR_SIZE(num_pages) \
	((PAGES_PER_BITFIELD_ELEM * (0 != (num_pages % PAGES_PER_BITFIELD_ELEM)) + \
	num_pages) / PAGES_PER_BITFIELD_ELEM)

/**
 * small_granularity_alloc() - Allocate 1-32 power-of-two pages.
 * @epma_dev: protected memory allocator device structure.
 * @alloc_bitfield_idx: index of the relevant bitfield.
 * @start_bit: starting bitfield index.
 * @order: bitshift for number of pages. Order of 0 to 5 equals 1 to 32 pages.
 * @pma: protected_memory_allocation struct.
 *
 * Allocate a power-of-two number of pages, N, where
 * 0 <= N <= ORDER_OF_PAGES_PER_BITFIELD_ELEM - 1.  ie, Up to 32 pages. The routine
 * fills-in a pma structure and sets the appropriate bits in the allocated-pages
 * bitfield array but assumes the caller has already determined that these are
 * already clear.
 *
 * This routine always works within only a single allocated-pages bitfield element.
 * It can be thought of as the 'small-granularity' allocator.
 */
static void small_granularity_alloc(struct simple_pma_device *const epma_dev,
				    size_t alloc_bitfield_idx, size_t start_bit,
				    size_t order,
				    struct protected_memory_allocation *pma)
{
	size_t i;
	size_t page_idx;
	u64 *bitfield;
	size_t alloc_pages_bitfield_size;

	if (WARN_ON(!epma_dev) ||
	    WARN_ON(!pma))
		return;

	WARN(epma_dev->rmem_size == 0, "%s: rmem_size is 0", __func__);
	alloc_pages_bitfield_size = ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size);

	WARN(alloc_bitfield_idx >= alloc_pages_bitfield_size,
	     "%s: idx>bf_size: %zu %zu", __func__,
	     alloc_bitfield_idx, alloc_pages_bitfield_size);

	WARN((start_bit + (1 << order)) > PAGES_PER_BITFIELD_ELEM,
	     "%s: start=%zu order=%zu ppbe=%zu",
	     __func__, start_bit, order, PAGES_PER_BITFIELD_ELEM);

	bitfield = &epma_dev->allocated_pages_bitfield_arr[alloc_bitfield_idx];

	for (i = 0; i < (1 << order); i++) {
		/* Check the pages represented by this bit are actually free */
		WARN(*bitfield & (1ULL << (start_bit + i)),
		      "in %s: page not free: %zu %zu %.16llx %zu\n",
		      __func__, i, order, *bitfield, alloc_pages_bitfield_size);

		/* Mark the pages as now allocated */
		*bitfield |= (1ULL << (start_bit + i));
	}

	/* Compute the page index */
	page_idx = (alloc_bitfield_idx * PAGES_PER_BITFIELD_ELEM) + start_bit;

	/* Fill-in the allocation struct for the caller */
	pma->pa = epma_dev->rmem_base + (page_idx << PAGE_SHIFT);
	pma->order = order;
}

/**
 * large_granularity_alloc() - Allocate pages at multiples of 64 pages.
 * @epma_dev: protected memory allocator device structure.
 * @start_alloc_bitfield_idx: index of the starting bitfield.
 * @order: bitshift for number of pages. Order of 6+ equals 64+ pages.
 * @pma: protected_memory_allocation struct.
 *
 * Allocate a power-of-two number of pages, N, where
 * N >= ORDER_OF_PAGES_PER_BITFIELD_ELEM. ie, 64 pages or more. The routine fills-in
 * a pma structure and sets the appropriate bits in the allocated-pages bitfield array
 * but assumes the caller has already determined that these are already clear.
 *
 * Unlike small_granularity_alloc, this routine can work with multiple 64-page groups,
 * ie multiple elements from the allocated-pages bitfield array. However, it always
 * works with complete sets of these 64-page groups. It can therefore be thought of
 * as the 'large-granularity' allocator.
 */
static void large_granularity_alloc(struct simple_pma_device *const epma_dev,
				    size_t start_alloc_bitfield_idx,
				    size_t order,
				    struct protected_memory_allocation *pma)
{
	size_t i;
	size_t num_pages_to_alloc = (size_t)1 << order;
	size_t num_bitfield_elements_needed = num_pages_to_alloc / PAGES_PER_BITFIELD_ELEM;
	size_t start_page_idx = start_alloc_bitfield_idx * PAGES_PER_BITFIELD_ELEM;

	if (WARN_ON(!epma_dev) ||
	    WARN_ON(!pma))
		return;

	/*
	 * Are there anough bitfield array elements (groups of 64 pages)
	 * between the start element and the end of the bitfield array
	 * to fulfill the request?
	 */
	WARN((start_alloc_bitfield_idx + order) >= ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size),
	     "%s: start=%zu order=%zu ms=%zu",
	     __func__, start_alloc_bitfield_idx, order, epma_dev->rmem_size);

	for (i = 0; i < num_bitfield_elements_needed; i++) {
		u64 *bitfield = &epma_dev->allocated_pages_bitfield_arr[start_alloc_bitfield_idx + i];

		/* We expect all pages that relate to this bitfield element to be free */
		WARN((*bitfield != 0),
		     "in %s: pages not free: i=%zu o=%zu bf=%.16llx\n",
		     __func__, i, order, *bitfield);

		/* Mark all the pages for this element as not free */
		*bitfield = ~0ULL;
	}

	/* Fill-in the allocation struct for the caller */
	pma->pa = epma_dev->rmem_base + (start_page_idx  << PAGE_SHIFT);
	pma->order = order;
}

static struct protected_memory_allocation *simple_pma_alloc_page(
	struct protected_memory_allocator_device *pma_dev, unsigned int order)
{
	struct simple_pma_device *const epma_dev =
		container_of(pma_dev, struct simple_pma_device, pma_dev);
	struct protected_memory_allocation *pma;
	size_t num_pages_to_alloc;

	u64 *bitfields = epma_dev->allocated_pages_bitfield_arr;
	size_t i;
	size_t bit;
	size_t count;

	dev_dbg(epma_dev->dev, "%s(pma_dev=%px, order=%u\n",
		__func__, (void *)pma_dev, order);

	/* This is an example function that follows an extremely simple logic
	 * and is very likely to fail to allocate memory if put under stress.
	 *
	 * The simple_pma_device maintains an array of u64s, with one bit used
	 * to track the status of each page.
	 *
	 * In order to create a memory allocation, the allocator looks for an
	 * adjacent group of cleared bits. This does leave the algorithm open
	 * to fragmentation issues, but is deemed sufficient for now.
	 * If successful, the allocator shall mark all the pages as allocated
	 * and increment the offset accordingly.
	 *
	 * Allocations of 64 pages or more (order 6) can be allocated only with
	 * 64-page alignment, in order to keep the algorithm as simple as
	 * possible. ie, starting from bit 0 of any 64-bit page-allocation
	 * bitfield. For this, the large-granularity allocator is utilised.
	 *
	 * Allocations of lower-order can only be allocated entirely within the
	 * same group of 64 pages, with the small-ganularity allocator  (ie
	 * always from the same 64-bit page-allocation bitfield) - again, to
	 * keep things as simple as possible, but flexible to meet
	 * current needs.
	 */

	num_pages_to_alloc = (size_t)1 << order;

	pma = devm_kzalloc(epma_dev->dev, sizeof(*pma), GFP_KERNEL);
	if (!pma) {
		dev_err(epma_dev->dev, "Failed to alloc pma struct");
		return NULL;
	}

	spin_lock(&epma_dev->rmem_lock);

	if (epma_dev->num_free_pages < num_pages_to_alloc) {
		dev_err(epma_dev->dev, "not enough free pages\n");
		devm_kfree(epma_dev->dev, pma);
		spin_unlock(&epma_dev->rmem_lock);
		return NULL;
	}

	/*
	 * For order 0-5 (ie, 1 to 32 pages) we always allocate within the same set of 64 pages
	 * Currently, most allocations will be very small (1 page), so the more likely path
	 * here is order < ORDER_OF_PAGES_PER_BITFIELD_ELEM.
	 */
	if (likely(order < ORDER_OF_PAGES_PER_BITFIELD_ELEM)) {
		size_t alloc_pages_bitmap_size = ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size);

		for (i = 0; i < alloc_pages_bitmap_size; i++) {
			count = 0;

			for (bit = 0; bit < PAGES_PER_BITFIELD_ELEM; bit++) {
				if  (0 == (bitfields[i] & (1ULL << bit))) {
					if ((count + 1) >= num_pages_to_alloc) {
						/*
						 * We've found enough free, consecutive pages with which to
						 * make an allocation
						 */
						small_granularity_alloc(
							epma_dev, i,
							bit - count, order,
							pma);

						epma_dev->num_free_pages -=
							num_pages_to_alloc;

						spin_unlock(
							&epma_dev->rmem_lock);
						return pma;
					}

					/* So far so good, but we need more set bits yet */
					count++;
				} else {
					/*
					 * We found an allocated page, so nothing we've seen so far can be used.
					 * Keep looking.
					 */
					count = 0;
				}
			}
		}
	} else {
		/**
		 * For allocations of order ORDER_OF_PAGES_PER_BITFIELD_ELEM and above (>= 64 pages), we know
		 * we'll only get allocations for whole groups of 64 pages, which hugely simplifies the task.
		 */
		size_t alloc_pages_bitmap_size = ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size);

		/* How many 64-bit bitfield elements will be needed for the allocation? */
		size_t num_bitfield_elements_needed = num_pages_to_alloc / PAGES_PER_BITFIELD_ELEM;

		count = 0;

		for (i = 0; i < alloc_pages_bitmap_size; i++) {
			/* Are all the pages free for the i'th u64 bitfield element? */
			if (bitfields[i] == 0) {
				count += PAGES_PER_BITFIELD_ELEM;

				if (count >= (1 << order)) {
					size_t start_idx = (i + 1) - num_bitfield_elements_needed;

					large_granularity_alloc(epma_dev,
								start_idx,
								order, pma);

					epma_dev->num_free_pages -= 1 << order;
					spin_unlock(&epma_dev->rmem_lock);
					return pma;
				}
			} else {
				count = 0;
			}
		}
	}

	spin_unlock(&epma_dev->rmem_lock);
	devm_kfree(epma_dev->dev, pma);

	dev_err(epma_dev->dev, "not enough contiguous pages (need %zu), total free pages left %zu\n",
		num_pages_to_alloc, epma_dev->num_free_pages);
	return NULL;
}

static phys_addr_t simple_pma_get_phys_addr(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma)
{
	struct simple_pma_device *const epma_dev =
		container_of(pma_dev, struct simple_pma_device, pma_dev);

	dev_dbg(epma_dev->dev, "%s(pma_dev=%px, pma=%px, pa=%llx\n",
		__func__, (void *)pma_dev, (void *)pma,
		(unsigned long long)pma->pa);

	return pma->pa;
}

static void simple_pma_free_page(
	struct protected_memory_allocator_device *pma_dev,
	struct protected_memory_allocation *pma)
{
	struct simple_pma_device *const epma_dev =
		container_of(pma_dev, struct simple_pma_device, pma_dev);
	size_t num_pages_in_allocation;
	size_t offset;
	size_t i;
	size_t bitfield_idx;
	size_t bitfield_start_bit;
	size_t page_num;
	u64 *bitfield;
	size_t alloc_pages_bitmap_size;
	size_t num_bitfield_elems_used_by_alloc;

	WARN_ON(pma == NULL);

	dev_dbg(epma_dev->dev, "%s(pma_dev=%px, pma=%px, pa=%llx\n",
		__func__, (void *)pma_dev, (void *)pma,
		(unsigned long long)pma->pa);

	WARN_ON(pma->pa < epma_dev->rmem_base);

	/* This is an example function that follows an extremely simple logic
	 * and is vulnerable to abuse.
	 */
	offset = (pma->pa - epma_dev->rmem_base);
	num_pages_in_allocation = (size_t)1 << pma->order;

	/* The number of bitfield elements used by the allocation */
	num_bitfield_elems_used_by_alloc = num_pages_in_allocation / PAGES_PER_BITFIELD_ELEM;

	/* The page number of the first page of the allocation, relative to rmem_base */
	page_num = offset >> PAGE_SHIFT;

	/* Which u64 bitfield refers to this page? */
	bitfield_idx = page_num / PAGES_PER_BITFIELD_ELEM;

	alloc_pages_bitmap_size = ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size);

	/* Is the allocation within expected bounds? */
	WARN_ON((bitfield_idx + num_bitfield_elems_used_by_alloc) >= alloc_pages_bitmap_size);

	spin_lock(&epma_dev->rmem_lock);

	if (pma->order < ORDER_OF_PAGES_PER_BITFIELD_ELEM) {
		bitfield = &epma_dev->allocated_pages_bitfield_arr[bitfield_idx];

		/* Which bit within that u64 bitfield is the lsb covering this allocation?  */
		bitfield_start_bit = page_num % PAGES_PER_BITFIELD_ELEM;

		/* Clear the bits for the pages we're now freeing */
		*bitfield &= ~(((1ULL << num_pages_in_allocation) - 1) << bitfield_start_bit);
	} else {
		WARN(page_num % PAGES_PER_BITFIELD_ELEM,
		     "%s: Expecting allocs of order >= %d to be %zu-page aligned\n",
		     __func__, ORDER_OF_PAGES_PER_BITFIELD_ELEM, PAGES_PER_BITFIELD_ELEM);

		for (i = 0; i < num_bitfield_elems_used_by_alloc; i++) {
			bitfield = &epma_dev->allocated_pages_bitfield_arr[bitfield_idx + i];

			/* We expect all bits to be set (all pages allocated) */
			WARN((*bitfield != ~0),
			     "%s: alloc being freed is not fully allocated: of=%zu np=%zu bf=%.16llx\n",
			     __func__, offset, num_pages_in_allocation, *bitfield);

			/*
			 * Now clear all the bits in the bitfield element to mark all the pages
			 * it refers to as free.
			 */
			*bitfield = 0ULL;
		}
	}

	epma_dev->num_free_pages += num_pages_in_allocation;
	spin_unlock(&epma_dev->rmem_lock);
	devm_kfree(epma_dev->dev, pma);
}

static int protected_memory_allocator_probe(struct platform_device *pdev)
{
	struct simple_pma_device *epma_dev;
	struct device_node *np;
	phys_addr_t rmem_base;
	size_t rmem_size;
	size_t alloc_bitmap_pages_arr_size;
#if (KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE)
	struct reserved_mem *rmem;
#endif

	np = pdev->dev.of_node;

	if (!np) {
		dev_err(&pdev->dev, "device node pointer not set\n");
		return -ENODEV;
	}

	np = of_parse_phandle(np, "memory-region", 0);
	if (!np) {
		dev_err(&pdev->dev, "memory-region node not set\n");
		return -ENODEV;
	}

#if (KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE)
	rmem = of_reserved_mem_lookup(np);
	if (rmem) {
		rmem_base = rmem->base;
		rmem_size = rmem->size >> PAGE_SHIFT;
	} else
#endif
	{
		of_node_put(np);
		dev_err(&pdev->dev, "could not read reserved memory-region\n");
		return -ENODEV;
	}

	of_node_put(np);
	epma_dev = devm_kzalloc(&pdev->dev, sizeof(*epma_dev), GFP_KERNEL);
	if (!epma_dev)
		return -ENOMEM;

	epma_dev->pma_dev.ops.pma_alloc_page = simple_pma_alloc_page;
	epma_dev->pma_dev.ops.pma_get_phys_addr = simple_pma_get_phys_addr;
	epma_dev->pma_dev.ops.pma_free_page = simple_pma_free_page;
	epma_dev->pma_dev.owner = THIS_MODULE;
	epma_dev->dev = &pdev->dev;
	epma_dev->rmem_base = rmem_base;
	epma_dev->rmem_size = rmem_size;
	epma_dev->num_free_pages = rmem_size;
	spin_lock_init(&epma_dev->rmem_lock);

	alloc_bitmap_pages_arr_size = ALLOC_PAGES_BITFIELD_ARR_SIZE(epma_dev->rmem_size);

	epma_dev->allocated_pages_bitfield_arr = devm_kzalloc(&pdev->dev,
		alloc_bitmap_pages_arr_size * BITFIELD_ELEM_SIZE, GFP_KERNEL);

	if (!epma_dev->allocated_pages_bitfield_arr) {
		dev_err(&pdev->dev, "failed to allocate resources\n");
		devm_kfree(&pdev->dev, epma_dev);
		return -ENOMEM;
	}

	if (epma_dev->rmem_size % PAGES_PER_BITFIELD_ELEM) {
		size_t extra_pages =
			alloc_bitmap_pages_arr_size * PAGES_PER_BITFIELD_ELEM -
			epma_dev->rmem_size;
		size_t last_bitfield_index = alloc_bitmap_pages_arr_size - 1;

		/* Mark the extra pages (that lie outside the reserved range) as
		 * always in use.
		 */
		epma_dev->allocated_pages_bitfield_arr[last_bitfield_index] =
			((1ULL << extra_pages) - 1) <<
			(PAGES_PER_BITFIELD_ELEM - extra_pages);
	}

	platform_set_drvdata(pdev, &epma_dev->pma_dev);
	dev_info(&pdev->dev,
		"Protected memory allocator probed successfully\n");
	dev_info(&pdev->dev, "Protected memory region: base=%llx num pages=%zu\n",
		(unsigned long long)rmem_base, rmem_size);

	return 0;
}

static int protected_memory_allocator_remove(struct platform_device *pdev)
{
	struct protected_memory_allocator_device *pma_dev =
		platform_get_drvdata(pdev);
	struct simple_pma_device *epma_dev;
	struct device *dev;

	if (!pma_dev)
		return -EINVAL;

	epma_dev = container_of(pma_dev, struct simple_pma_device, pma_dev);
	dev = epma_dev->dev;

	if (epma_dev->num_free_pages < epma_dev->rmem_size) {
		dev_warn(&pdev->dev, "Leaking %zu pages of protected memory\n",
			epma_dev->rmem_size - epma_dev->num_free_pages);
	}

	platform_set_drvdata(pdev, NULL);
	devm_kfree(dev, epma_dev->allocated_pages_bitfield_arr);
	devm_kfree(dev, epma_dev);

	dev_info(&pdev->dev,
		"Protected memory allocator removed successfully\n");

	return 0;
}

static const struct of_device_id protected_memory_allocator_dt_ids[] = {
	{ .compatible = "arm,protected-memory-allocator" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, protected_memory_allocator_dt_ids);

static struct platform_driver protected_memory_allocator_driver = {
	.probe = protected_memory_allocator_probe,
	.remove = protected_memory_allocator_remove,
	.driver = {
		.name = "simple_protected_memory_allocator",
		.of_match_table = of_match_ptr(protected_memory_allocator_dt_ids),
	}
};

module_platform_driver(protected_memory_allocator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION("1.0");
