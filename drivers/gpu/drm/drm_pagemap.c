// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * Copyright © 2024-2025 Intel Corporation
 */

#include <linux/dma-mapping.h>
#include <linux/migrate.h>
#include <linux/pagemap.h>
#include <drm/drm_drv.h>
#include <drm/drm_pagemap.h>

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
 * @device_private_page_owner: Device private pages owner
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
	void *device_private_page_owner;
};

/**
 * drm_pagemap_zdd_alloc() - Allocate a zdd structure.
 * @device_private_page_owner: Device private pages owner
 *
 * This function allocates and initializes a new zdd structure. It sets up the
 * reference count and initializes the destroy work.
 *
 * Return: Pointer to the allocated zdd on success, ERR_PTR() on failure.
 */
static struct drm_pagemap_zdd *
drm_pagemap_zdd_alloc(void *device_private_page_owner)
{
	struct drm_pagemap_zdd *zdd;

	zdd = kmalloc(sizeof(*zdd), GFP_KERNEL);
	if (!zdd)
		return NULL;

	kref_init(&zdd->refcount);
	zdd->devmem_allocation = NULL;
	zdd->device_private_page_owner = device_private_page_owner;

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

	if (devmem) {
		complete_all(&devmem->detached);
		if (devmem->ops->devmem_release)
			devmem->ops->devmem_release(devmem);
	}
	kfree(zdd);
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
	zone_device_page_init(page);
}

/**
 * drm_pagemap_migrate_map_pages() - Map migration pages for GPU SVM migration
 * @dev: The device for which the pages are being mapped
 * @pagemap_addr: Array to store DMA information corresponding to mapped pages
 * @migrate_pfn: Array of migrate page frame numbers to map
 * @npages: Number of pages to map
 * @dir: Direction of data transfer (e.g., DMA_BIDIRECTIONAL)
 *
 * This function maps pages of memory for migration usage in GPU SVM. It
 * iterates over each page frame number provided in @migrate_pfn, maps the
 * corresponding page, and stores the DMA address in the provided @dma_addr
 * array.
 *
 * Returns: 0 on success, -EFAULT if an error occurs during mapping.
 */
static int drm_pagemap_migrate_map_pages(struct device *dev,
					 struct drm_pagemap_addr *pagemap_addr,
					 unsigned long *migrate_pfn,
					 unsigned long npages,
					 enum dma_data_direction dir)
{
	unsigned long i;

	for (i = 0; i < npages;) {
		struct page *page = migrate_pfn_to_page(migrate_pfn[i]);
		dma_addr_t dma_addr;
		struct folio *folio;
		unsigned int order = 0;

		if (!page)
			goto next;

		if (WARN_ON_ONCE(is_zone_device_page(page)))
			return -EFAULT;

		folio = page_folio(page);
		order = folio_order(folio);

		dma_addr = dma_map_page(dev, page, 0, page_size(page), dir);
		if (dma_mapping_error(dev, dma_addr))
			return -EFAULT;

		pagemap_addr[i] =
			drm_pagemap_addr_encode(dma_addr,
						DRM_INTERCONNECT_SYSTEM,
						order, dir);

next:
		i += NR_PAGES(order);
	}

	return 0;
}

/**
 * drm_pagemap_migrate_unmap_pages() - Unmap pages previously mapped for GPU SVM migration
 * @dev: The device for which the pages were mapped
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
					    unsigned long npages,
					    enum dma_data_direction dir)
{
	unsigned long i;

	for (i = 0; i < npages;) {
		if (!pagemap_addr[i].addr || dma_mapping_error(dev, pagemap_addr[i].addr))
			goto next;

		dma_unmap_page(dev, pagemap_addr[i].addr, PAGE_SIZE << pagemap_addr[i].order, dir);

next:
		i += NR_PAGES(pagemap_addr[i].order);
	}
}

static unsigned long
npages_in_range(unsigned long start, unsigned long end)
{
	return (end - start) >> PAGE_SHIFT;
}

/**
 * drm_pagemap_migrate_to_devmem() - Migrate a struct mm_struct range to device memory
 * @devmem_allocation: The device memory allocation to migrate to.
 * The caller should hold a reference to the device memory allocation,
 * and the reference is consumed by this function unless it returns with
 * an error.
 * @mm: Pointer to the struct mm_struct.
 * @start: Start of the virtual address range to migrate.
 * @end: End of the virtual address range to migrate.
 * @timeslice_ms: The time requested for the migrated pagemap pages to
 * be present in @mm before being allowed to be migrated back.
 * @pgmap_owner: Not used currently, since only system memory is considered.
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
				  unsigned long timeslice_ms,
				  void *pgmap_owner)
{
	const struct drm_pagemap_devmem_ops *ops = devmem_allocation->ops;
	struct migrate_vma migrate = {
		.start		= start,
		.end		= end,
		.pgmap_owner	= pgmap_owner,
		.flags		= MIGRATE_VMA_SELECT_SYSTEM,
	};
	unsigned long i, npages = npages_in_range(start, end);
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

	zdd = drm_pagemap_zdd_alloc(pgmap_owner);
	if (!zdd) {
		err = -ENOMEM;
		goto err_free;
	}

	migrate.vma = vas;
	migrate.src = buf;
	migrate.dst = migrate.src + npages;

	err = migrate_vma_setup(&migrate);
	if (err)
		goto err_free;

	if (!migrate.cpages) {
		err = -EFAULT;
		goto err_free;
	}

	if (migrate.cpages != npages) {
		err = -EBUSY;
		goto err_finalize;
	}

	err = ops->populate_devmem_pfn(devmem_allocation, npages, migrate.dst);
	if (err)
		goto err_finalize;

	err = drm_pagemap_migrate_map_pages(devmem_allocation->dev, pagemap_addr,
					    migrate.src, npages, DMA_TO_DEVICE);

	if (err)
		goto err_finalize;

	for (i = 0; i < npages; ++i) {
		struct page *page = pfn_to_page(migrate.dst[i]);

		pages[i] = page;
		migrate.dst[i] = migrate_pfn(migrate.dst[i]);
		drm_pagemap_get_devmem_page(page, zdd);
	}

	err = ops->copy_to_devmem(pages, pagemap_addr, npages);
	if (err)
		goto err_finalize;

	/* Upon success bind devmem allocation to range and zdd */
	devmem_allocation->timeslice_expiration = get_jiffies_64() +
		msecs_to_jiffies(timeslice_ms);
	zdd->devmem_allocation = devmem_allocation;	/* Owns ref */

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, migrate.dst);
	migrate_vma_pages(&migrate);
	migrate_vma_finalize(&migrate);
	drm_pagemap_migrate_unmap_pages(devmem_allocation->dev, pagemap_addr, npages,
					DMA_TO_DEVICE);
err_free:
	if (zdd)
		drm_pagemap_zdd_put(zdd);
	kvfree(buf);
err_out:
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

	err = drm_pagemap_migrate_map_pages(devmem_allocation->dev, pagemap_addr,
					    dst, npages, DMA_FROM_DEVICE);
	if (err)
		goto err_finalize;

	for (i = 0; i < npages; ++i)
		pages[i] = migrate_pfn_to_page(src[i]);

	err = ops->copy_to_ram(pages, pagemap_addr, npages);
	if (err)
		goto err_finalize;

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, dst);
	migrate_device_pages(src, dst, npages);
	migrate_device_finalize(src, dst, npages);
	drm_pagemap_migrate_unmap_pages(devmem_allocation->dev, pagemap_addr, npages,
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
 * @device_private_page_owner: Device private pages owner
 * @page: Pointer to the page for fault handling (can be NULL)
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
					void *device_private_page_owner,
					struct page *page,
					unsigned long fault_addr,
					unsigned long size)
{
	struct migrate_vma migrate = {
		.vma		= vas,
		.pgmap_owner	= device_private_page_owner,
		.flags		= MIGRATE_VMA_SELECT_DEVICE_PRIVATE |
		MIGRATE_VMA_SELECT_DEVICE_COHERENT,
		.fault_page	= page,
	};
	struct drm_pagemap_zdd *zdd;
	const struct drm_pagemap_devmem_ops *ops;
	struct device *dev = NULL;
	unsigned long npages, mpages = 0;
	struct page **pages;
	struct drm_pagemap_addr *pagemap_addr;
	unsigned long start, end;
	void *buf;
	int i, err = 0;

	if (page) {
		zdd = page->zone_device_data;
		if (time_before64(get_jiffies_64(),
				  zdd->devmem_allocation->timeslice_expiration))
			return 0;
	}

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

	if (!page) {
		for (i = 0; i < npages; ++i) {
			if (!(migrate.src[i] & MIGRATE_PFN_MIGRATE))
				continue;

			page = migrate_pfn_to_page(migrate.src[i]);
			break;
		}

		if (!page)
			goto err_finalize;
	}
	zdd = page->zone_device_data;
	ops = zdd->devmem_allocation->ops;
	dev = zdd->devmem_allocation->dev;

	err = drm_pagemap_migrate_populate_ram_pfn(vas, page, npages, &mpages,
						   migrate.src, migrate.dst,
						   start);
	if (err)
		goto err_finalize;

	err = drm_pagemap_migrate_map_pages(dev, pagemap_addr, migrate.dst, npages,
					    DMA_FROM_DEVICE);
	if (err)
		goto err_finalize;

	for (i = 0; i < npages; ++i)
		pages[i] = migrate_pfn_to_page(migrate.src[i]);

	err = ops->copy_to_ram(pages, pagemap_addr, npages);
	if (err)
		goto err_finalize;

err_finalize:
	if (err)
		drm_pagemap_migration_unlock_put_pages(npages, migrate.dst);
	migrate_vma_pages(&migrate);
	migrate_vma_finalize(&migrate);
	if (dev)
		drm_pagemap_migrate_unmap_pages(dev, pagemap_addr, npages,
						DMA_FROM_DEVICE);
err_free:
	kvfree(buf);
err_out:

	return err;
}

/**
 * drm_pagemap_page_free() - Put GPU SVM zone device data associated with a page
 * @page: Pointer to the page
 *
 * This function is a callback used to put the GPU SVM zone device data
 * associated with a page when it is being released.
 */
static void drm_pagemap_page_free(struct page *page)
{
	drm_pagemap_zdd_put(page->zone_device_data);
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
					   zdd->device_private_page_owner,
					   vmf->page, vmf->address,
					   zdd->devmem_allocation->size);

	return err ? VM_FAULT_SIGBUS : 0;
}

static const struct dev_pagemap_ops drm_pagemap_pagemap_ops = {
	.page_free = drm_pagemap_page_free,
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
 */
void drm_pagemap_devmem_init(struct drm_pagemap_devmem *devmem_allocation,
			     struct device *dev, struct mm_struct *mm,
			     const struct drm_pagemap_devmem_ops *ops,
			     struct drm_pagemap *dpagemap, size_t size)
{
	init_completion(&devmem_allocation->detached);
	devmem_allocation->dev = dev;
	devmem_allocation->mm = mm;
	devmem_allocation->ops = ops;
	devmem_allocation->dpagemap = dpagemap;
	devmem_allocation->size = size;
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
