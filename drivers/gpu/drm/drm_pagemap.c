// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright © 2024-2025 Intel Corporation
 */

#include <linux/dma-fence.h>
#include <linux/dma-mapping.h>
#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <drm/drm_drv.h>
#include <drm/drm_pagemap.h>
#include <drm/drm_pagemap_util.h>
#include <drm/drm_print.h>

/**
 * DOC: Overview
 *
 * The DRM pagemap layer is intended to augment the dev_pagemap functionality by
 * providing a way to populate a struct mm_struct virtual range with device
 * private pages and to provide helpers to abstract device memory allocations,
 * to migrate memory back and forth between device memory and system RAM and
 * to handle access (and in the future migration) between devices implementing
 * a fast interconnect that is not necessarily visible to the rest of the
 * system.
 *
 * Typically the DRM pagemap receives requests from one or more DRM GPU SVM
 * instances to populate struct mm_struct virtual ranges with memory, and the
 * migration is best effort only and may thus fail. The implementation should
 * also handle device unbinding by blocking (return an -ENODEV) error for new
 * population requests and after that migrate all device pages to system ram.
 */

/**
 * DOC: Migration
 *
 * Migration granularity typically follows the GPU SVM range requests, but
 * if there are clashes, due to races or due to the fact that multiple GPU
 * SVM instances have different views of the ranges used, and because of that
 * parts of a requested range is already present in the requested device memory,
 * the implementation has a variety of options. It can fail and it can choose
 * to populate only the part of the range that isn't already in device memory,
 * and it can evict the range to system before trying to migrate. Ideally an
 * implementation would just try to migrate the missing part of the range and
 * allocate just enough memory to do so.
 *
 * When migrating to system memory as a response to a cpu fault or a device
 * memory eviction request, currently a full device memory allocation is
 * migrated back to system. Moving forward this might need improvement for
 * situations where a single page needs bouncing between system memory and
 * device memory due to, for example, atomic operations.
 *
 * Key DRM pagemap components:
 *
 * - Device Memory Allocations:
 *      Embedded structure containing enough information for the drm_pagemap to
 *      migrate to / from device memory.
 *
 * - Device Memory Operations:
 *      Define the interface for driver-specific device memory operations
 *      release memory, populate pfns, and copy to / from device memory.
 */

/**
 * struct drm_pagemap_zdd - GPU SVM zone device data
 *
 * @refcount: Reference count for the zdd
 * @devmem_allocation: device memory allocation
 * @dpagemap: Refcounted pointer to the underlying struct drm_pagemap.
 *
 * This structure serves as a generic wrapper installed in
 * page->zone_device_data. It provides infrastructure for looking up a device
 * memory allocation upon CPU page fault and asynchronously releasing device
 * memory once the CPU has no page references. Asynchronous release is useful
 * because CPU page references can be dropped in IRQ contexts, while releasing
 * device memory likely requires sleeping locks.
 */
struct drm_pagemap_zdd {
	struct kref refcount;
	struct drm_pagemap_devmem *devmem_allocation;
	struct drm_pagemap *dpagemap;
};

/**
 * drm_pagemap_zdd_alloc() - Allocate a zdd structure.
 * @dpagemap: Pointer to the underlying struct drm_pagemap.
 *
 * This function allocates and initializes a new zdd structure. It sets up the
 * reference count and initializes the destroy work.
 *
 * Return: Pointer to the allocated zdd on success, ERR_PTR() on failure.
 */
static struct drm_pagemap_zdd *
drm_pagemap_zdd_alloc(struct drm_pagemap *dpagemap)
{
	struct drm_pagemap_zdd *zdd;

	zdd = kmalloc(sizeof(*zdd), GFP_KERNEL);
	if (!zdd)
		return NULL;

	kref_init(&zdd->refcount);
	zdd->devmem_allocation = NULL;
	zdd->dpagemap = drm_pagemap_get(dpagemap);

	return zdd;
}

/**
 * drm_pagemap_zdd_get() - Get a reference to a zdd structure.
 * @zdd: Pointer to the zdd structure.
 *
 * This function increments the reference count of the provided zdd structure.
 *
 * Return: Pointer to the zdd structure.
 */
static struct drm_pagemap_zdd *drm_pagemap_zdd_get(struct drm_pagemap_zdd *zdd)
{
	kref_get(&zdd->refcount);
	return zdd;
}

/**
 * drm_pagemap_zdd_destroy() - Destroy a zdd structure.
 * @ref: Pointer to the reference count structure.
 *
 * This function queues the destroy_work of the zdd for asynchronous destruction.
 */
static void drm_pagemap_zdd_destroy(struct kref *ref)
{
	struct drm_pagemap_zdd *zdd =
		container_of(ref, struct drm_pagemap_zdd, refcount);
	struct drm_pagemap_devmem *devmem = zdd->devmem_allocation;
	struct drm_pagemap *dpagemap = zdd->dpagemap;

	if (devmem) {
		complete_all(&devmem->detached);
		if (devmem->ops->devmem_release)
			devmem->ops->devmem_release(devmem);
	}
	kfree(zdd);
	drm_pagemap_put(dpagemap);
}

/**
 * drm_pagemap_zdd_put() - Put a zdd reference.
 * @zdd: Pointer to the zdd structure.
 *
 * This function decrements the reference count of the provided zdd structure
 * and schedules its destruction if the count drops to zero.
 */
static void drm_pagemap_zdd_put(struct drm_pagemap_zdd *zdd)
{
	kref_put(&zdd->refcount, drm_pagemap_zdd_destroy);
}

/**
 * drm_pagemap_migration_unlock_put_page() - Put a migration page
 * @page: Pointer to the page to put
 *
 * This function unlocks and puts a page.
 */
static void drm_pagemap_migration_unlock_put_page(struct page *page)
{
	unlock_page(page);
	put_page(page);
}

/**
 * drm_pagemap_migration_unlock_put_pages() - Put migration pages
 * @npages: Number of pages
 * @migrate_pfn: Array of migrate page frame numbers
 *
 * This function unlocks and puts an array of pages.
 */
static void drm_pagemap_migration_unlock_put_pages(unsigned long npages,
						   unsigned long *migrate_pfn)
{
	unsigned long i;

	for (i = 0; i < npages; ++i) {
		struct page *page;

		if (!migrate_pfn[i])
			continue;

		page = migrate_pfn_to_page(migrate_pfn[i]);
		drm_pagemap_migration_unlock_put_page(page);
		migrate_pfn[i] = 0;
	}
}

/**
 * drm_pagemap_get_devmem_page() - Get a reference to a device memory page
 * @page: Pointer to the page
 * @zdd: Pointer to the GPU SVM zone device data
 *
 * This function associates the given page with the specified GPU SVM zone
 * device data and initializes it for zone device usage.
 */
static void drm_pagemap_get_devmem_page(struct page *page,
					struct drm_pagemap_zdd *zdd)
{
	page->zone_device_data = drm_pagemap_zdd_get(zdd);
	zone_device_page_init(page, page_pgmap(page), 0);
}

/**
 * drm_pagemap_migrate_map_pages() - Map migration pages for GPU SVM migration
 * @dev: The device performing the migration.
 * @local_dpagemap: The drm_pagemap local to the migrating device.
 * @pagemap_addr: Array to store DMA information corresponding to mapped pages.
 * @migrate_pfn: Array of page frame numbers of system pages or peer pages to map.
 * @npages: Number of system pages or peer pages to map.
 * @dir: Direction of data transfer (e.g., DMA_BIDIRECTIONAL)
 * @mdetails: Details governing the migration behaviour.
 *
 * This function maps pages of memory for migration usage in GPU SVM. It
 * iterates over each page frame number provided in @migrate_pfn, maps the
 * corresponding page, and stores the DMA address in the provided @dma_addr
 * array.
 *
 * Returns: 0 on success, -EFAULT if an error occurs during mapping.
 */
static int drm_pagemap_migrate_map_pages(struct device *dev,
					 struct drm_pagemap *local_dpagemap,
					 struct drm_pagemap_addr *pagemap_addr,
					 unsigned long *migrate_pfn,
					 unsigned long npages,
					 enum dma_data_direction dir,
					 const struct drm_pagemap_migrate_details *mdetails)
{
	unsigned long num_peer_pages = 0, num_local_pages = 0, i;

	for (i = 0; i < npages;) {
		struct page *page = migrate_pfn_to_page(migrate_pfn[i]);
		dma_addr_t dma_addr;
		struct folio *folio;
		unsigned int order = 0;

		if (!page)
			goto next;

		folio = page_folio(page);
		order = folio_order(folio);

		if (is_device_private_page(page)) {
			struct drm_pagemap_zdd *zdd = page->zone_device_data;
			struct drm_pagemap *dpagemap = zdd->dpagemap;
			struct drm_pagemap_addr addr;

			if (dpagemap == local_dpagemap) {
				if (!mdetails->can_migrate_same_pagemap)
					goto next;

				num_local_pages += NR_PAGES(order);
			} else {
				num_peer_pages += NR_PAGES(order);
			}

			addr = dpagemap->ops->device_map(dpagemap, dev, page, order, dir);
			if (dma_mapping_error(dev, addr.addr))
				return -EFAULT;

			pagemap_addr[i] = addr;
		} else {
			dma_addr = dma_map_page(dev, page, 0, page_size(page), dir);
			if (dma_mapping_error(dev, dma_addr))
				return -EFAULT;

			pagemap_addr[i] =
				drm_pagemap_addr_encode(dma_addr,
							DRM_INTERCONNECT_SYSTEM,
							order, dir);
		}

next:
		i += NR_PAGES(order);
	}

	if (num_peer_pages)
		drm_dbg(local_dpagemap->drm, "Migrating %lu peer pages over interconnect.\n",
			num_peer_pages);
	if (num_local_pages)
		drm_dbg(local_dpagemap->drm, "Migrating %lu local pages over interconnect.\n",
			num_local_pages);

	return 0;
}

/**
 * drm_pagemap_migrate_unmap_pages() - Unmap pages previously mapped for GPU SVM migration
 * @dev: The device for which the pages were mapped
 * @migrate_pfn: Array of migrate pfns set up for the mapped pages. Used to
 * determine the drm_pagemap of a peer device private page.
 * @pagemap_addr: Array of DMA information corresponding to mapped pages
 * @npages: Number of pages to unmap
 * @dir: Direction of data transfer (e.g., DMA_BIDIRECTIONAL)
 *
 * This function unmaps previously mapped pages of memory for GPU Shared Virtual
 * Memory (SVM). It iterates over each DMA address provided in @dma_addr, checks
 * if it's valid and not already unmapped, and unmaps the corresponding page.
 */
static void drm_pagemap_migrate_unmap_pages(struct device *dev,
					    struct drm_pagemap_addr *pagemap_addr,
					    unsigned long *migrate_pfn,
					    unsigned long npages,
					    enum dma_data_direction dir)
{
	unsigned long i;

	for (i = 0; i < npages;) {
		struct page *page = migrate_pfn_to_page(migrate_pfn[i]);

		if (!page || !pagemap_addr[i].addr || dma_mapping_error(dev, pagemap_addr[i].addr))
			goto next;

		if (is_zone_device_page(page)) {
			struct drm_pagemap_zdd *zdd = page->zone_device_data;
			struct drm_pagemap *dpagemap = zdd->dpagemap;

			dpagemap->ops->device_unmap(dpagemap, dev, pagemap_addr[i]);
		} else {
			dma_unmap_page(dev, pagemap_addr[i].addr,
				       PAGE_SIZE << pagemap_addr[i].order, dir);
		}

next:
		i += NR_PAGES(pagemap_addr[i].order);
	}
}

static unsigned long
npages_in_range(unsigned long start, unsigned long end)
{
	return (end - start) >> PAGE_SHIFT;
}

static int
drm_pagemap_migrate_remote_to_local(struct drm_pagemap_devmem *devmem,
				    struct device *remote_device,
				    struct drm_pagemap *remote_dpagemap,
				    unsigned long local_pfns[],
				    struct page *remote_pages[],
				    struct drm_pagemap_addr pagemap_addr[],
				    unsigned long npages,
				    const struct drm_pagemap_devmem_ops *ops,
				    const struct drm_pagemap_migrate_details *mdetails)

{
	int err = drm_pagemap_migrate_map_pages(remote_device, remote_dpagemap,
						pagemap_addr, local_pfns,
						npages, DMA_FROM_DEVICE, mdetails);

	if (err)
		goto out;

	err = ops->copy_to_ram(remote_pages, pagemap_addr, npages,
			       devmem->pre_migrate_fence);
out:
	drm_pagemap_migrate_unmap_pages(remote_device, pagemap_addr, local_pfns,
					npages, DMA_FROM_DEVICE);
	return err;
}

static int
drm_pagemap_migrate_sys_to_dev(struct drm_pagemap_devmem *devmem,
			       unsigned long sys_pfns[],
			       struct page *local_pages[],
			       struct drm_pagemap_addr pagemap_addr[],
			       unsigned long npages,
			       const struct drm_pagemap_devmem_ops *ops,
			       const struct drm_pagemap_migrate_details *mdetails)
{
	int err = drm_pagemap_migrate_map_pages(devmem->dev, devmem->dpagemap,
						pagemap_addr, sys_pfns, npages,
						DMA_TO_DEVICE, mdetails);

	if (err)
		goto out;

	err = ops->copy_to_devmem(local_pages, pagemap_addr, npages,
				  devmem->pre_migrate_fence);
out:
	drm_pagemap_migrate_unmap_pages(devmem->dev, pagemap_addr, sys_pfns, npages,
					DMA_TO_DEVICE);
	return err;
}

/**
 * struct migrate_range_loc - Cursor into the loop over migrate_pfns for migrating to
 * device.
 * @start: The current loop index.
 * @device: migrating device.
 * @dpagemap: Pointer to struct drm_pagemap used by the migrating device.
 * @ops: The copy ops to be used for the migrating device.
 */
struct migrate_range_loc {
	unsigned long start;
	struct device *device;
	struct drm_pagemap *dpagemap;
	const struct drm_pagemap_devmem_ops *ops;
};

static int drm_pagemap_migrate_range(struct drm_pagemap_devmem *devmem,
				     unsigned long src_pfns[],
				     unsigned long dst_pfns[],
				     struct page *pages[],
				     struct drm_pagemap_addr pagemap_addr[],
				     struct migrate_range_loc *last,
				     const struct migrate_range_loc *cur,
				     const struct drm_pagemap_migrate_details *mdetails)
{
	int ret = 0;

	if (cur->start == 0)
		goto out;

	if (cur->start <= last->start)
		return 0;

	if (cur->dpagemap == last->dpagemap && cur->ops == last->ops)
		return 0;

	if (last->dpagemap)
		ret = drm_pagemap_migrate_remote_to_local(devmem,
							  last->device,
							  last->dpagemap,
							  &dst_pfns[last->start],
							  &pages[last->start],
							  &pagemap_addr[last->start],
							  cur->start - last->start,
							  last->ops, mdetails);

	else
		ret = drm_pagemap_migrate_sys_to_dev(devmem,
						     &src_pfns[last->start],
						     &pages[last->start],
						     &pagemap_addr[last->start],
						     cur->start - last->start,
						     last->ops, mdetails);

out:
	*last = *cur;
	return ret;
}

/**
 * drm_pagemap_migrate_to_devmem() - Migrate a struct mm_struct range to device memory
 * @devmem_allocation: The device memory allocation to migrate to.
 * The caller should hold a reference to the device memory allocation,
 * and the reference is consumed by this function even if it returns with
 * an error.
 * @mm: Pointer to the struct mm_struct.
 * @start: Start of the virtual address range to migrate.
 * @end: End of the virtual address range to migrate.
 * @mdetails: Details to govern the migration.
 *
 * This function migrates the specified virtual address range to device memory.
 * It performs the necessary setup and invokes the driver-specific operations for
 * migration to device memory. Expected to be called while holding the mmap lock in
 * at least read mode.
 *
 * Note: The @timeslice_ms parameter can typically be used to force data to
 * remain in pagemap pages long enough for a GPU to perform a task and to prevent
 * a migration livelock. One alternative would be for the GPU driver to block
 * in a mmu_notifier for the specified amount of time, but adding the
 * functionality to the pagemap is likely nicer to the system as a whole.
 *
 * Return: %0 on success, negative error code on failure.
 */
int drm_pagemap_migrate_to_devmem(struct drm_pagemap_devmem *devmem_allocation,
				  struct mm_struct *mm,
				  unsigned long start, unsigned long end,
				  const struct drm_pagemap_migrate_details *mdetails)
{
	const struct drm_pagemap_devmem_ops *ops = devmem_allocation->ops;
	struct drm_pagemap *dpagemap = devmem_allocation->dpagemap;
	struct dev_pagemap *pagemap = dpagemap->pagemap;
	struct migrate_vma migrate = {
		.start		= start,
		.end		= end,
		.pgmap_owner	= pagemap->owner,
		/*
		 * FIXME: MIGRATE_VMA_SELECT_DEVICE_PRIVATE intermittently
		 * causes 'xe_exec_system_allocator --r *race*no*' to trigger aa
		 * engine reset and a hard hang due to getting stuck on a folio
		 * lock. This should work and needs to be root-caused. The only
		 * downside of not selecting MIGRATE_VMA_SELECT_DEVICE_PRIVATE
		 * is that device-to-device migrations won’t work; instead,
		 * memory will bounce through system memory. This path should be
		 * rare and only occur when the madvise attributes of memory are
		 * changed or atomics are being used.
		 */
		.flags		= MIGRATE_VMA_SELECT_SYSTEM | MIGRATE_VMA_SELECT_DEVICE_COHERENT,
	};
	unsigned long i, npages = npages_in_range(start, end);
	unsigned long own_pages = 0, migrated_pages = 0;
	struct migrate_range_loc cur, last = {.device = dpagemap->drm->dev, .ops = ops};
	struct vm_area_struct *vas;
	struct drm_pagemap_zdd *zdd = NULL;
	struct page **pages;
	struct drm_pagemap_addr *pagemap_addr;
	void *buf;
	int err;

	mmap_assert_locked(mm);

	if (!ops->populate_devmem_pfn || !ops->copy_to_devmem ||
	    !ops->copy_to_ram)
		return -EOPNOTSUPP;

	vas = vma_lookup(mm, start);
	if (!vas) {
		err = -ENOENT;
		goto err_out;
	}

	if (end > vas->vm_end || start < vas->vm_start) {
		err = -EINVAL;
		goto err_out;
	}

	if (!vma_is_anonymous(vas)) {
		err = -EBUSY;
		goto err_out;
	}

	buf = kvcalloc(npages, 2 * sizeof(*migrate.src) + sizeof(*pagemap_addr) +
		       sizeof(*pages), GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}
	pagemap_addr = buf + (2 * sizeof(*migrate.src) * npages);
	pages = buf + (2 * sizeof(*migrate.src) + sizeof(*pagemap_addr)) * npages;

	zdd = drm_pagemap_zdd_alloc(dpagemap);
	if (!zdd) {
		err = -ENOMEM;
		kvfree(buf);
		goto err_out;
	}
	zdd->devmem_allocation = devmem_allocation;	/* Owns ref */

	migrate.vma = vas;
	migrate.src = buf;
	migrate.dst = migrate.src + npages;

	err = migrate_vma_setup(&migrate);
	if (err)
		goto err_free;

	if (!migrate.cpages) {
		/* No pages to migrate. Raced or unknown device pages. */
		err = -EBUSY;
		goto err_free;
	}

	if (migrate.cpages != npages) {
		/*
		 * Some pages to migrate. But we want to migrate all or
		 * nothing. Raced or unknown device pages.
		 */
		err = -EBUSY;
		goto err_aborted_migration;
	}

	/* Count device-private pages to migrate */
	for (i = 0; i < npages;) {
		struct page *src_page = migrate_pfn_to_page(migrate.src[i]);
		unsigned long nr_pages = src_page ? NR_PAGES(folio_order(page_folio(src_page))) : 1;

		if (src_page && is_zone_device_page(src_page)) {
			if (page_pgmap(src_page) == pagemap)
				own_pages += nr_pages;
		}

		i += nr_pages;
	}

	drm_dbg(dpagemap->drm, "Total pages %lu; Own pages: %lu.\n",
		npages, own_pages);
	if (own_pages == npages) {
		err = 0;
		drm_dbg(dpagemap->drm, "Migration wasn't necessary.\n");
		goto err_aborted_migration;
	} else if (own_pages && !mdetails->can_migrate_same_pagemap) {
		err = -EBUSY;
		drm_dbg(dpagemap->drm, "Migration aborted due to fragmentation.\n");
		goto err_aborted_migration;
	}

	err = ops->populate_devmem_pfn(devmem_allocation, npages, migrate.dst);
	if (err)
		goto err_aborted_migration;

	own_pages = 0;

	for (i = 0; i < npages; ++i) {
		struct page *page = pfn_to_page(migrate.dst[i]);
		struct page *src_page = migrate_pfn_to_page(migrate.src[i]);
		cur.start = i;

		pages[i] = NULL;
		if (src_page && is_device_private_page(src_page)) {
			struct drm_pagemap_zdd *src_zdd = src_page->zone_device_data;

			if (page_pgmap(src_page) == pagemap &&
			    !mdetails->can_migrate_same_pagemap) {
				migrate.dst[i] = 0;
				own_pages++;
				continue;
			}
			if (mdetails->source_peer_migrates) {
				cur.dpagemap = src_zdd->dpagemap;
				cur.ops = src_zdd->devmem_allocation->ops;
				cur.device = cur.dpagemap->drm->dev;
				pages[i] = src_page;
			}
		}
		if (!pages[i]) {
			cur.dpagemap = NULL;
			cur.ops = ops;
			cur.device = dpagemap->drm->dev;
			pages[i] = page;
		}
		migrate.dst[i] = migrate_pfn(migrate.dst[i]);
		drm_pagemap_get_devmem_page(page, zdd);

		/* If we switched the migrating drm_pagemap, migrate previous pages now */
		err = drm_pagemap_migrate_range(devmem_allocation, migrate.src, migrate.dst,
						pages, pagemap_addr, &last, &cur,
						mdetails);
		if (err) {
			npages = i + 1;
			goto err_finalize;
		}
	}
	cur.start = npages;
	cur.ops = NULL; /* Force migration */
	err = drm_pagemap_migrate_range(devmem_allocation, migrate.src, migrate.dst,
					pages, pagemap_addr, &last, &cur, mdetails);
	if (err)
		goto err_finalize;

	drm_WARN_ON(dpagemap->drm, !!own_pages);

	dma_fence_put(devmem_allocation->pre_migrate_fence);
	devmem_allocation->pre_migrate_fence = NULL;

	/* Upon success bind devmem allocation to range and zdd */
	devmem_allocation->timeslice_expiration = get_jiffies_64() +
		msecs_to_jiffies(mdetails->timeslice_ms);

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, migrate.dst);
err_aborted_migration:
	migrate_vma_pages(&migrate);

	for (i = 0; !err && i < npages;) {
		struct page *page = migrate_pfn_to_page(migrate.src[i]);
		unsigned long nr_pages = page ? NR_PAGES(folio_order(page_folio(page))) : 1;

		if (migrate.src[i] & MIGRATE_PFN_MIGRATE)
			migrated_pages += nr_pages;

		i += nr_pages;
	}

	if (!err && migrated_pages < npages - own_pages) {
		drm_dbg(dpagemap->drm, "Raced while finalizing migration.\n");
		err = -EBUSY;
	}

	migrate_vma_finalize(&migrate);
err_free:
	drm_pagemap_zdd_put(zdd);
	kvfree(buf);
	return err;

err_out:
	devmem_allocation->ops->devmem_release(devmem_allocation);
	return err;
}
EXPORT_SYMBOL_GPL(drm_pagemap_migrate_to_devmem);

/**
 * drm_pagemap_migrate_populate_ram_pfn() - Populate RAM PFNs for a VM area
 * @vas: Pointer to the VM area structure, can be NULL
 * @fault_page: Fault page
 * @npages: Number of pages to populate
 * @mpages: Number of pages to migrate
 * @src_mpfn: Source array of migrate PFNs
 * @mpfn: Array of migrate PFNs to populate
 * @addr: Start address for PFN allocation
 *
 * This function populates the RAM migrate page frame numbers (PFNs) for the
 * specified VM area structure. It allocates and locks pages in the VM area for
 * RAM usage. If vas is non-NULL use alloc_page_vma for allocation, if NULL use
 * alloc_page for allocation.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int drm_pagemap_migrate_populate_ram_pfn(struct vm_area_struct *vas,
						struct page *fault_page,
						unsigned long npages,
						unsigned long *mpages,
						unsigned long *src_mpfn,
						unsigned long *mpfn,
						unsigned long addr)
{
	unsigned long i;

	for (i = 0; i < npages;) {
		struct page *page = NULL, *src_page;
		struct folio *folio;
		unsigned int order = 0;

		if (!(src_mpfn[i] & MIGRATE_PFN_MIGRATE))
			goto next;

		src_page = migrate_pfn_to_page(src_mpfn[i]);
		if (!src_page)
			goto next;

		if (fault_page) {
			if (src_page->zone_device_data !=
			    fault_page->zone_device_data)
				goto next;
		}

		order = folio_order(page_folio(src_page));

		/* TODO: Support fallback to single pages if THP allocation fails */
		if (vas)
			folio = vma_alloc_folio(GFP_HIGHUSER, order, vas, addr);
		else
			folio = folio_alloc(GFP_HIGHUSER, order);

		if (!folio)
			goto free_pages;

		page = folio_page(folio, 0);
		mpfn[i] = migrate_pfn(page_to_pfn(page));

next:
		if (page)
			addr += page_size(page);
		else
			addr += PAGE_SIZE;

		i += NR_PAGES(order);
	}

	for (i = 0; i < npages;) {
		struct page *page = migrate_pfn_to_page(mpfn[i]);
		unsigned int order = 0;

		if (!page)
			goto next_lock;

		WARN_ON_ONCE(!folio_trylock(page_folio(page)));

		order = folio_order(page_folio(page));
		*mpages += NR_PAGES(order);

next_lock:
		i += NR_PAGES(order);
	}

	return 0;

free_pages:
	for (i = 0; i < npages;) {
		struct page *page = migrate_pfn_to_page(mpfn[i]);
		unsigned int order = 0;

		if (!page)
			goto next_put;

		put_page(page);
		mpfn[i] = 0;

		order = folio_order(page_folio(page));

next_put:
		i += NR_PAGES(order);
	}
	return -ENOMEM;
}

static void drm_pagemap_dev_unhold_work(struct work_struct *work);
static LLIST_HEAD(drm_pagemap_unhold_list);
static DECLARE_WORK(drm_pagemap_work, drm_pagemap_dev_unhold_work);

/**
 * struct drm_pagemap_dev_hold - Struct to aid in drm_device release.
 * @link: Link into drm_pagemap_unhold_list for deferred reference releases.
 * @drm: drm device to put.
 *
 * When a struct drm_pagemap is released, we also need to release the
 * reference it holds on the drm device. However, typically that needs
 * to be done separately from a system-wide workqueue.
 * Each time a struct drm_pagemap is initialized
 * (or re-initialized if cached) therefore allocate a separate
 * drm_pagemap_dev_hold item, from which we put the drm device and
 * associated module.
 */
struct drm_pagemap_dev_hold {
	struct llist_node link;
	struct drm_device *drm;
};

static void drm_pagemap_release(struct kref *ref)
{
	struct drm_pagemap *dpagemap = container_of(ref, typeof(*dpagemap), ref);
	struct drm_pagemap_dev_hold *dev_hold = dpagemap->dev_hold;

	/*
	 * We know the pagemap provider is alive at this point, since
	 * the struct drm_pagemap_dev_hold holds a reference to the
	 * pagemap provider drm_device and its module.
	 */
	dpagemap->dev_hold = NULL;
	drm_pagemap_shrinker_add(dpagemap);
	llist_add(&dev_hold->link, &drm_pagemap_unhold_list);
	schedule_work(&drm_pagemap_work);
	/*
	 * Here, either the provider device is still alive, since if called from
	 * page_free(), the caller is holding a reference on the dev_pagemap,
	 * or if called from drm_pagemap_put(), the direct caller is still alive.
	 * This ensures we can't race with THIS module unload.
	 */
}

static void drm_pagemap_dev_unhold_work(struct work_struct *work)
{
	struct llist_node *node = llist_del_all(&drm_pagemap_unhold_list);
	struct drm_pagemap_dev_hold *dev_hold, *next;

	/*
	 * Deferred release of drm_pagemap provider device and module.
	 * THIS module is kept alive during the release by the
	 * flush_work() in the drm_pagemap_exit() function.
	 */
	llist_for_each_entry_safe(dev_hold, next, node, link) {
		struct drm_device *drm = dev_hold->drm;
		struct module *module = drm->driver->fops->owner;

		drm_dbg(drm, "Releasing reference on provider device and module.\n");
		drm_dev_put(drm);
		module_put(module);
		kfree(dev_hold);
	}
}

static struct drm_pagemap_dev_hold *
drm_pagemap_dev_hold(struct drm_pagemap *dpagemap)
{
	struct drm_pagemap_dev_hold *dev_hold;
	struct drm_device *drm = dpagemap->drm;

	dev_hold = kzalloc(sizeof(*dev_hold), GFP_KERNEL);
	if (!dev_hold)
		return ERR_PTR(-ENOMEM);

	init_llist_node(&dev_hold->link);
	dev_hold->drm = drm;
	(void)try_module_get(drm->driver->fops->owner);
	drm_dev_get(drm);

	return dev_hold;
}

/**
 * drm_pagemap_reinit() - Reinitialize a drm_pagemap
 * @dpagemap: The drm_pagemap to reinitialize
 *
 * Reinitialize a drm_pagemap, for which drm_pagemap_release
 * has already been called. This interface is intended for the
 * situation where the driver caches a destroyed drm_pagemap.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_pagemap_reinit(struct drm_pagemap *dpagemap)
{
	dpagemap->dev_hold = drm_pagemap_dev_hold(dpagemap);
	if (IS_ERR(dpagemap->dev_hold))
		return PTR_ERR(dpagemap->dev_hold);

	kref_init(&dpagemap->ref);
	return 0;
}
EXPORT_SYMBOL(drm_pagemap_reinit);

/**
 * drm_pagemap_init() - Initialize a pre-allocated drm_pagemap
 * @dpagemap: The drm_pagemap to initialize.
 * @pagemap: The associated dev_pagemap providing the device
 * private pages.
 * @drm: The drm device. The drm_pagemap holds a reference on the
 * drm_device and the module owning the drm_device until
 * drm_pagemap_release(). This facilitates drm_pagemap exporting.
 * @ops: The drm_pagemap ops.
 *
 * Initialize and take an initial reference on a drm_pagemap.
 * After successful return, use drm_pagemap_put() to destroy.
 *
 ** Return: 0 on success, negative error code on error.
 */
int drm_pagemap_init(struct drm_pagemap *dpagemap,
		     struct dev_pagemap *pagemap,
		     struct drm_device *drm,
		     const struct drm_pagemap_ops *ops)
{
	kref_init(&dpagemap->ref);
	dpagemap->ops = ops;
	dpagemap->pagemap = pagemap;
	dpagemap->drm = drm;
	dpagemap->cache = NULL;
	INIT_LIST_HEAD(&dpagemap->shrink_link);

	return drm_pagemap_reinit(dpagemap);
}
EXPORT_SYMBOL(drm_pagemap_init);

/**
 * drm_pagemap_put() - Put a struct drm_pagemap reference
 * @dpagemap: Pointer to a struct drm_pagemap object.
 *
 * Puts a struct drm_pagemap reference and frees the drm_pagemap object
 * if the refount reaches zero.
 */
void drm_pagemap_put(struct drm_pagemap *dpagemap)
{
	if (likely(dpagemap)) {
		drm_pagemap_shrinker_might_lock(dpagemap);
		kref_put(&dpagemap->ref, drm_pagemap_release);
	}
}
EXPORT_SYMBOL(drm_pagemap_put);

/**
 * drm_pagemap_evict_to_ram() - Evict GPU SVM range to RAM
 * @devmem_allocation: Pointer to the device memory allocation
 *
 * Similar to __drm_pagemap_migrate_to_ram but does not require mmap lock and
 * migration done via migrate_device_* functions.
 *
 * Return: 0 on success, negative error code on failure.
 */
int drm_pagemap_evict_to_ram(struct drm_pagemap_devmem *devmem_allocation)
{
	const struct drm_pagemap_devmem_ops *ops = devmem_allocation->ops;
	struct drm_pagemap_migrate_details mdetails = {};
	unsigned long npages, mpages = 0;
	struct page **pages;
	unsigned long *src, *dst;
	struct drm_pagemap_addr *pagemap_addr;
	void *buf;
	int i, err = 0;
	unsigned int retry_count = 2;

	npages = devmem_allocation->size >> PAGE_SHIFT;

retry:
	if (!mmget_not_zero(devmem_allocation->mm))
		return -EFAULT;

	buf = kvcalloc(npages, 2 * sizeof(*src) + sizeof(*pagemap_addr) +
		       sizeof(*pages), GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}
	src = buf;
	dst = buf + (sizeof(*src) * npages);
	pagemap_addr = buf + (2 * sizeof(*src) * npages);
	pages = buf + (2 * sizeof(*src) + sizeof(*pagemap_addr)) * npages;

	err = ops->populate_devmem_pfn(devmem_allocation, npages, src);
	if (err)
		goto err_free;

	err = migrate_device_pfns(src, npages);
	if (err)
		goto err_free;

	err = drm_pagemap_migrate_populate_ram_pfn(NULL, NULL, npages, &mpages,
						   src, dst, 0);
	if (err || !mpages)
		goto err_finalize;

	err = drm_pagemap_migrate_map_pages(devmem_allocation->dev,
					    devmem_allocation->dpagemap, pagemap_addr,
					    dst, npages, DMA_FROM_DEVICE,
					    &mdetails);
	if (err)
		goto err_finalize;

	for (i = 0; i < npages; ++i)
		pages[i] = migrate_pfn_to_page(src[i]);

	err = ops->copy_to_ram(pages, pagemap_addr, npages, NULL);
	if (err)
		goto err_finalize;

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, dst);
	migrate_device_pages(src, dst, npages);
	migrate_device_finalize(src, dst, npages);
	drm_pagemap_migrate_unmap_pages(devmem_allocation->dev, pagemap_addr, dst, npages,
					DMA_FROM_DEVICE);

err_free:
	kvfree(buf);
err_out:
	mmput_async(devmem_allocation->mm);

	if (completion_done(&devmem_allocation->detached))
		return 0;

	if (retry_count--) {
		cond_resched();
		goto retry;
	}

	return err ?: -EBUSY;
}
EXPORT_SYMBOL_GPL(drm_pagemap_evict_to_ram);

/**
 * __drm_pagemap_migrate_to_ram() - Migrate GPU SVM range to RAM (internal)
 * @vas: Pointer to the VM area structure
 * @page: Pointer to the page for fault handling.
 * @fault_addr: Fault address
 * @size: Size of migration
 *
 * This internal function performs the migration of the specified GPU SVM range
 * to RAM. It sets up the migration, populates + dma maps RAM PFNs, and
 * invokes the driver-specific operations for migration to RAM.
 *
 * Return: 0 on success, negative error code on failure.
 */
static int __drm_pagemap_migrate_to_ram(struct vm_area_struct *vas,
					struct page *page,
					unsigned long fault_addr,
					unsigned long size)
{
	struct migrate_vma migrate = {
		.vma		= vas,
		.pgmap_owner	= page_pgmap(page)->owner,
		.flags		= MIGRATE_VMA_SELECT_DEVICE_PRIVATE |
		MIGRATE_VMA_SELECT_DEVICE_COHERENT,
		.fault_page	= page,
	};
	struct drm_pagemap_migrate_details mdetails = {};
	struct drm_pagemap_zdd *zdd;
	const struct drm_pagemap_devmem_ops *ops;
	struct device *dev = NULL;
	unsigned long npages, mpages = 0;
	struct page **pages;
	struct drm_pagemap_addr *pagemap_addr;
	unsigned long start, end;
	void *buf;
	int i, err = 0;

	zdd = page->zone_device_data;
	if (time_before64(get_jiffies_64(), zdd->devmem_allocation->timeslice_expiration))
		return 0;

	start = ALIGN_DOWN(fault_addr, size);
	end = ALIGN(fault_addr + 1, size);

	/* Corner where VMA area struct has been partially unmapped */
	if (start < vas->vm_start)
		start = vas->vm_start;
	if (end > vas->vm_end)
		end = vas->vm_end;

	migrate.start = start;
	migrate.end = end;
	npages = npages_in_range(start, end);

	buf = kvcalloc(npages, 2 * sizeof(*migrate.src) + sizeof(*pagemap_addr) +
		       sizeof(*pages), GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto err_out;
	}
	pagemap_addr = buf + (2 * sizeof(*migrate.src) * npages);
	pages = buf + (2 * sizeof(*migrate.src) + sizeof(*pagemap_addr)) * npages;

	migrate.vma = vas;
	migrate.src = buf;
	migrate.dst = migrate.src + npages;

	err = migrate_vma_setup(&migrate);
	if (err)
		goto err_free;

	/* Raced with another CPU fault, nothing to do */
	if (!migrate.cpages)
		goto err_free;

	ops = zdd->devmem_allocation->ops;
	dev = zdd->devmem_allocation->dev;

	err = drm_pagemap_migrate_populate_ram_pfn(vas, page, npages, &mpages,
						   migrate.src, migrate.dst,
						   start);
	if (err)
		goto err_finalize;

	err = drm_pagemap_migrate_map_pages(dev, zdd->dpagemap, pagemap_addr, migrate.dst, npages,
					    DMA_FROM_DEVICE, &mdetails);
	if (err)
		goto err_finalize;

	for (i = 0; i < npages; ++i)
		pages[i] = migrate_pfn_to_page(migrate.src[i]);

	err = ops->copy_to_ram(pages, pagemap_addr, npages, NULL);
	if (err)
		goto err_finalize;

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, migrate.dst);
	migrate_vma_pages(&migrate);
	migrate_vma_finalize(&migrate);
	if (dev)
		drm_pagemap_migrate_unmap_pages(dev, pagemap_addr, migrate.dst,
						npages, DMA_FROM_DEVICE);
err_free:
	kvfree(buf);
err_out:

	return err;
}

/**
 * drm_pagemap_folio_free() - Put GPU SVM zone device data associated with a folio
 * @folio: Pointer to the folio
 *
 * This function is a callback used to put the GPU SVM zone device data
 * associated with a page when it is being released.
 */
static void drm_pagemap_folio_free(struct folio *folio)
{
	drm_pagemap_zdd_put(folio->page.zone_device_data);
}

/**
 * drm_pagemap_migrate_to_ram() - Migrate a virtual range to RAM (page fault handler)
 * @vmf: Pointer to the fault information structure
 *
 * This function is a page fault handler used to migrate a virtual range
 * to ram. The device memory allocation in which the device page is found is
 * migrated in its entirety.
 *
 * Returns:
 * VM_FAULT_SIGBUS on failure, 0 on success.
 */
static vm_fault_t drm_pagemap_migrate_to_ram(struct vm_fault *vmf)
{
	struct drm_pagemap_zdd *zdd = vmf->page->zone_device_data;
	int err;

	err = __drm_pagemap_migrate_to_ram(vmf->vma,
					   vmf->page, vmf->address,
					   zdd->devmem_allocation->size);

	return err ? VM_FAULT_SIGBUS : 0;
}

static const struct dev_pagemap_ops drm_pagemap_pagemap_ops = {
	.folio_free = drm_pagemap_folio_free,
	.migrate_to_ram = drm_pagemap_migrate_to_ram,
};

/**
 * drm_pagemap_pagemap_ops_get() - Retrieve GPU SVM device page map operations
 *
 * Returns:
 * Pointer to the GPU SVM device page map operations structure.
 */
const struct dev_pagemap_ops *drm_pagemap_pagemap_ops_get(void)
{
	return &drm_pagemap_pagemap_ops;
}
EXPORT_SYMBOL_GPL(drm_pagemap_pagemap_ops_get);

/**
 * drm_pagemap_devmem_init() - Initialize a drm_pagemap device memory allocation
 *
 * @devmem_allocation: The struct drm_pagemap_devmem to initialize.
 * @dev: Pointer to the device structure which device memory allocation belongs to
 * @mm: Pointer to the mm_struct for the address space
 * @ops: Pointer to the operations structure for GPU SVM device memory
 * @dpagemap: The struct drm_pagemap we're allocating from.
 * @size: Size of device memory allocation
 * @pre_migrate_fence: Fence to wait for or pipeline behind before migration starts.
 * (May be NULL).
 */
void drm_pagemap_devmem_init(struct drm_pagemap_devmem *devmem_allocation,
			     struct device *dev, struct mm_struct *mm,
			     const struct drm_pagemap_devmem_ops *ops,
			     struct drm_pagemap *dpagemap, size_t size,
			     struct dma_fence *pre_migrate_fence)
{
	init_completion(&devmem_allocation->detached);
	devmem_allocation->dev = dev;
	devmem_allocation->mm = mm;
	devmem_allocation->ops = ops;
	devmem_allocation->dpagemap = dpagemap;
	devmem_allocation->size = size;
	devmem_allocation->pre_migrate_fence = pre_migrate_fence;
}
EXPORT_SYMBOL_GPL(drm_pagemap_devmem_init);

/**
 * drm_pagemap_page_to_dpagemap() - Return a pointer the drm_pagemap of a page
 * @page: The struct page.
 *
 * Return: A pointer to the struct drm_pagemap of a device private page that
 * was populated from the struct drm_pagemap. If the page was *not* populated
 * from a struct drm_pagemap, the result is undefined and the function call
 * may result in dereferencing and invalid address.
 */
struct drm_pagemap *drm_pagemap_page_to_dpagemap(struct page *page)
{
	struct drm_pagemap_zdd *zdd = page->zone_device_data;

	return zdd->devmem_allocation->dpagemap;
}
EXPORT_SYMBOL_GPL(drm_pagemap_page_to_dpagemap);

/**
 * drm_pagemap_populate_mm() - Populate a virtual range with device memory pages
 * @dpagemap: Pointer to the drm_pagemap managing the device memory
 * @start: Start of the virtual range to populate.
 * @end: End of the virtual range to populate.
 * @mm: Pointer to the virtual address space.
 * @timeslice_ms: The time requested for the migrated pagemap pages to
 * be present in @mm before being allowed to be migrated back.
 *
 * Attempt to populate a virtual range with device memory pages,
 * clearing them or migrating data from the existing pages if necessary.
 * The function is best effort only, and implementations may vary
 * in how hard they try to satisfy the request.
 *
 * Return: %0 on success, negative error code on error. If the hardware
 * device was removed / unbound the function will return %-ENODEV.
 */
int drm_pagemap_populate_mm(struct drm_pagemap *dpagemap,
			    unsigned long start, unsigned long end,
			    struct mm_struct *mm,
			    unsigned long timeslice_ms)
{
	int err;

	if (!mmget_not_zero(mm))
		return -EFAULT;
	mmap_read_lock(mm);
	err = dpagemap->ops->populate_mm(dpagemap, start, end, mm,
					 timeslice_ms);
	mmap_read_unlock(mm);
	mmput(mm);

	return err;
}
EXPORT_SYMBOL(drm_pagemap_populate_mm);

void drm_pagemap_destroy(struct drm_pagemap *dpagemap, bool is_atomic_or_reclaim)
{
	if (dpagemap->ops->destroy)
		dpagemap->ops->destroy(dpagemap, is_atomic_or_reclaim);
	else
		kfree(dpagemap);
}

static void drm_pagemap_exit(void)
{
	flush_work(&drm_pagemap_work);
	if (WARN_ON(!llist_empty(&drm_pagemap_unhold_list)))
		disable_work_sync(&drm_pagemap_work);
}
module_exit(drm_pagemap_exit);
