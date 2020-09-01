// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memremap.h>
#include <linux/slab.h>

#include <asm/page.h>

#include <xen/page.h>
#include <xen/xen.h>

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(page_list);
static unsigned int list_count;

static int fill_list(unsigned int nr_pages)
{
	struct dev_pagemap *pgmap;
	void *vaddr;
	unsigned int i, alloc_pages = round_up(nr_pages, PAGES_PER_SECTION);
	int ret;

	pgmap = kzalloc(sizeof(*pgmap), GFP_KERNEL);
	if (!pgmap)
		return -ENOMEM;

	pgmap->type = MEMORY_DEVICE_GENERIC;
	pgmap->res.name = "Xen scratch";
	pgmap->res.flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	ret = allocate_resource(&iomem_resource, &pgmap->res,
				alloc_pages * PAGE_SIZE, 0, -1,
				PAGES_PER_SECTION * PAGE_SIZE, NULL, NULL);
	if (ret < 0) {
		pr_err("Cannot allocate new IOMEM resource\n");
		kfree(pgmap);
		return ret;
	}

#ifdef CONFIG_XEN_HAVE_PVMMU
        /*
         * memremap will build page tables for the new memory so
         * the p2m must contain invalid entries so the correct
         * non-present PTEs will be written.
         *
         * If a failure occurs, the original (identity) p2m entries
         * are not restored since this region is now known not to
         * conflict with any devices.
         */
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		xen_pfn_t pfn = PFN_DOWN(pgmap->res.start);

		for (i = 0; i < alloc_pages; i++) {
			if (!set_phys_to_machine(pfn + i, INVALID_P2M_ENTRY)) {
				pr_warn("set_phys_to_machine() failed, no memory added\n");
				release_resource(&pgmap->res);
				kfree(pgmap);
				return -ENOMEM;
			}
                }
	}
#endif

	vaddr = memremap_pages(pgmap, NUMA_NO_NODE);
	if (IS_ERR(vaddr)) {
		pr_err("Cannot remap memory range\n");
		release_resource(&pgmap->res);
		kfree(pgmap);
		return PTR_ERR(vaddr);
	}

	for (i = 0; i < alloc_pages; i++) {
		struct page *pg = virt_to_page(vaddr + PAGE_SIZE * i);

		BUG_ON(!virt_addr_valid(vaddr + PAGE_SIZE * i));
		list_add(&pg->lru, &page_list);
		list_count++;
	}

	return 0;
}

/**
 * xen_alloc_unpopulated_pages - alloc unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int xen_alloc_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	unsigned int i;
	int ret = 0;

	mutex_lock(&list_lock);
	if (list_count < nr_pages) {
		ret = fill_list(nr_pages - list_count);
		if (ret)
			goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		struct page *pg = list_first_entry_or_null(&page_list,
							   struct page,
							   lru);

		BUG_ON(!pg);
		list_del(&pg->lru);
		list_count--;
		pages[i] = pg;

#ifdef CONFIG_XEN_HAVE_PVMMU
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			ret = xen_alloc_p2m_entry(page_to_pfn(pg));
			if (ret < 0) {
				unsigned int j;

				for (j = 0; j <= i; j++) {
					list_add(&pages[j]->lru, &page_list);
					list_count++;
				}
				goto out;
			}
		}
#endif
	}

out:
	mutex_unlock(&list_lock);
	return ret;
}
EXPORT_SYMBOL(xen_alloc_unpopulated_pages);

/**
 * xen_free_unpopulated_pages - return unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void xen_free_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	unsigned int i;

	mutex_lock(&list_lock);
	for (i = 0; i < nr_pages; i++) {
		list_add(&pages[i]->lru, &page_list);
		list_count++;
	}
	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(xen_free_unpopulated_pages);

#ifdef CONFIG_XEN_PV
static int __init init(void)
{
	unsigned int i;

	if (!xen_domain())
		return -ENODEV;

	if (!xen_pv_domain())
		return 0;

	/*
	 * Initialize with pages from the extra memory regions (see
	 * arch/x86/xen/setup.c).
	 */
	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		unsigned int j;

		for (j = 0; j < xen_extra_mem[i].n_pfns; j++) {
			struct page *pg =
				pfn_to_page(xen_extra_mem[i].start_pfn + j);

			list_add(&pg->lru, &page_list);
			list_count++;
		}
	}

	return 0;
}
subsys_initcall(init);
#endif
