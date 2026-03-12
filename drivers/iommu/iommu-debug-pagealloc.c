// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 * Author: Mostafa Saleh <smostafa@google.com>
 * IOMMU API debug page alloc sanitizer
 */
#include <linux/atomic.h>
#include <linux/iommu.h>
#include <linux/iommu-debug-pagealloc.h>
#include <linux/kernel.h>
#include <linux/page_ext.h>
#include <linux/page_owner.h>

#include "iommu-priv.h"

static bool needed;
DEFINE_STATIC_KEY_FALSE(iommu_debug_initialized);

struct iommu_debug_metadata {
	atomic_t ref;
};

static __init bool need_iommu_debug(void)
{
	return needed;
}

struct page_ext_operations page_iommu_debug_ops = {
	.size = sizeof(struct iommu_debug_metadata),
	.need = need_iommu_debug,
};

static struct iommu_debug_metadata *get_iommu_data(struct page_ext *page_ext)
{
	return page_ext_data(page_ext, &page_iommu_debug_ops);
}

static void iommu_debug_inc_page(phys_addr_t phys)
{
	struct page_ext *page_ext = page_ext_from_phys(phys);
	struct iommu_debug_metadata *d;

	if (!page_ext)
		return;

	d = get_iommu_data(page_ext);
	WARN_ON(atomic_inc_return_relaxed(&d->ref) <= 0);
	page_ext_put(page_ext);
}

static void iommu_debug_dec_page(phys_addr_t phys)
{
	struct page_ext *page_ext = page_ext_from_phys(phys);
	struct iommu_debug_metadata *d;

	if (!page_ext)
		return;

	d = get_iommu_data(page_ext);
	WARN_ON(atomic_dec_return_relaxed(&d->ref) < 0);
	page_ext_put(page_ext);
}

/*
 * IOMMU page size doesn't have to match the CPU page size. So, we use
 * the smallest IOMMU page size to refcount the pages in the vmemmap.
 * That is important as both map and unmap has to use the same page size
 * to update the refcount to avoid double counting the same page.
 * And as we can't know from iommu_unmap() what was the original page size
 * used for map, we just use the minimum supported one for both.
 */
static size_t iommu_debug_page_size(struct iommu_domain *domain)
{
	return 1UL << __ffs(domain->pgsize_bitmap);
}

static bool iommu_debug_page_count(const struct page *page)
{
	unsigned int ref;
	struct page_ext *page_ext = page_ext_get(page);
	struct iommu_debug_metadata *d = get_iommu_data(page_ext);

	ref = atomic_read(&d->ref);
	page_ext_put(page_ext);
	return ref != 0;
}

void __iommu_debug_check_unmapped(const struct page *page, int numpages)
{
	while (numpages--) {
		if (WARN_ON(iommu_debug_page_count(page))) {
			pr_warn("iommu: Detected page leak!\n");
			dump_page_owner(page);
		}
		page++;
	}
}

void __iommu_debug_map(struct iommu_domain *domain, phys_addr_t phys, size_t size)
{
	size_t off, end;
	size_t page_size = iommu_debug_page_size(domain);

	if (WARN_ON(!phys || check_add_overflow(phys, size, &end)))
		return;

	for (off = 0 ; off < size ; off += page_size)
		iommu_debug_inc_page(phys + off);
}

static void __iommu_debug_update_iova(struct iommu_domain *domain,
				      unsigned long iova, size_t size, bool inc)
{
	size_t off, end;
	size_t page_size = iommu_debug_page_size(domain);

	if (WARN_ON(check_add_overflow(iova, size, &end)))
		return;

	for (off = 0 ; off < size ; off += page_size) {
		phys_addr_t phys = iommu_iova_to_phys(domain, iova + off);

		if (!phys)
			continue;

		if (inc)
			iommu_debug_inc_page(phys);
		else
			iommu_debug_dec_page(phys);
	}
}

void __iommu_debug_unmap_begin(struct iommu_domain *domain,
			       unsigned long iova, size_t size)
{
	__iommu_debug_update_iova(domain, iova, size, false);
}

void __iommu_debug_unmap_end(struct iommu_domain *domain,
			     unsigned long iova, size_t size,
			     size_t unmapped)
{
	if ((unmapped == size) || WARN_ON_ONCE(unmapped > size))
		return;

	/* If unmap failed, re-increment the refcount. */
	__iommu_debug_update_iova(domain, iova + unmapped,
				  size - unmapped, true);
}

void iommu_debug_init(void)
{
	if (!needed)
		return;

	pr_info("iommu: Debugging page allocations, expect overhead or disable iommu.debug_pagealloc");
	static_branch_enable(&iommu_debug_initialized);
}

static int __init iommu_debug_pagealloc(char *str)
{
	return kstrtobool(str, &needed);
}
early_param("iommu.debug_pagealloc", iommu_debug_pagealloc);
