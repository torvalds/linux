// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022-2023 ARM Limited. All rights reserved.
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

/**
 * DOC: Base kernel page migration implementation.
 */
#include <linux/migrate.h>

#include <mali_kbase.h>
#include <mali_kbase_mem_migrate.h>
#include <mmu/mali_kbase_mmu.h>

/* Global integer used to determine if module parameter value has been
 * provided and if page migration feature is enabled.
 * Feature is disabled on all platforms by default.
 */
int kbase_page_migration_enabled;
module_param(kbase_page_migration_enabled, int, 0444);
KBASE_EXPORT_TEST_API(kbase_page_migration_enabled);

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static const struct movable_operations movable_ops;
#endif

bool kbase_alloc_page_metadata(struct kbase_device *kbdev, struct page *p, dma_addr_t dma_addr,
			       u8 group_id)
{
	struct kbase_page_metadata *page_md =
		kzalloc(sizeof(struct kbase_page_metadata), GFP_KERNEL);

	if (!page_md)
		return false;

	SetPagePrivate(p);
	set_page_private(p, (unsigned long)page_md);
	page_md->dma_addr = dma_addr;
	page_md->status = PAGE_STATUS_SET(page_md->status, (u8)ALLOCATE_IN_PROGRESS);
	page_md->vmap_count = 0;
	page_md->group_id = group_id;
	spin_lock_init(&page_md->migrate_lock);

	lock_page(p);
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
	__SetPageMovable(p, &movable_ops);
	page_md->status = PAGE_MOVABLE_SET(page_md->status);
#else
	/* In some corner cases, the driver may attempt to allocate memory pages
	 * even before the device file is open and the mapping for address space
	 * operations is created. In that case, it is impossible to assign address
	 * space operations to memory pages: simply pretend that they are movable,
	 * even if they are not.
	 *
	 * The page will go through all state transitions but it will never be
	 * actually considered movable by the kernel. This is due to the fact that
	 * the page cannot be marked as NOT_MOVABLE upon creation, otherwise the
	 * memory pool will always refuse to add it to the pool and schedule
	 * a worker thread to free it later.
	 *
	 * Page metadata may seem redundant in this case, but they are not,
	 * because memory pools expect metadata to be present when page migration
	 * is enabled and because the pages may always return to memory pools and
	 * gain the movable property later on in their life cycle.
	 */
	if (kbdev->mem_migrate.inode && kbdev->mem_migrate.inode->i_mapping) {
		__SetPageMovable(p, kbdev->mem_migrate.inode->i_mapping);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
	}
#endif
	unlock_page(p);

	return true;
}

static void kbase_free_page_metadata(struct kbase_device *kbdev, struct page *p, u8 *group_id)
{
	struct device *const dev = kbdev->dev;
	struct kbase_page_metadata *page_md;
	dma_addr_t dma_addr;

	page_md = kbase_page_private(p);
	if (!page_md)
		return;

	if (group_id)
		*group_id = page_md->group_id;
	dma_addr = kbase_dma_addr(p);
	dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	kfree(page_md);
	set_page_private(p, 0);
	ClearPagePrivate(p);
}

static void kbase_free_pages_worker(struct work_struct *work)
{
	struct kbase_mem_migrate *mem_migrate =
		container_of(work, struct kbase_mem_migrate, free_pages_work);
	struct kbase_device *kbdev = container_of(mem_migrate, struct kbase_device, mem_migrate);
	struct page *p, *tmp;
	struct kbase_page_metadata *page_md;
	LIST_HEAD(free_list);

	spin_lock(&mem_migrate->free_pages_lock);
	list_splice_init(&mem_migrate->free_pages_list, &free_list);
	spin_unlock(&mem_migrate->free_pages_lock);

	list_for_each_entry_safe(p, tmp, &free_list, lru) {
		u8 group_id = 0;
		list_del_init(&p->lru);

		lock_page(p);
		page_md = kbase_page_private(p);
		if (IS_PAGE_MOVABLE(page_md->status)) {
			__ClearPageMovable(p);
			page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		}
		unlock_page(p);

		kbase_free_page_metadata(kbdev, p, &group_id);
		kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev, group_id, p, 0);
	}
}

void kbase_free_page_later(struct kbase_device *kbdev, struct page *p)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	spin_lock(&mem_migrate->free_pages_lock);
	list_add(&p->lru, &mem_migrate->free_pages_list);
	spin_unlock(&mem_migrate->free_pages_lock);
}

/**
 * kbasep_migrate_page_pt_mapped - Migrate a memory page that is mapped
 *                                 in a PGD of kbase_mmu_table.
 *
 * @old_page:  Existing PGD page to remove
 * @new_page:  Destination for migrating the existing PGD page to
 *
 * Replace an existing PGD page with a new page by migrating its content. More specifically:
 * the new page shall replace the existing PGD page in the MMU page table. Before returning,
 * the new page shall be set as movable and not isolated, while the old page shall lose
 * the movable property. The meta data attached to the PGD page is transferred to the
 * new (replacement) page.
 *
 * Return: 0 on migration success, or -EAGAIN for a later retry. Otherwise it's a failure
 *          and the migration is aborted.
 */
static int kbasep_migrate_page_pt_mapped(struct page *old_page, struct page *new_page)
{
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_context *kctx = page_md->data.pt_mapped.mmut->kctx;
	struct kbase_device *kbdev = kctx->kbdev;
	dma_addr_t old_dma_addr = page_md->dma_addr;
	dma_addr_t new_dma_addr;
	int ret;

	/* Create a new dma map for the new page */
	new_dma_addr = dma_map_page(kbdev->dev, new_page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kbdev->dev, new_dma_addr))
		return -ENOMEM;

	/* Lock context to protect access to the page in physical allocation.
	 * This blocks the CPU page fault handler from remapping pages.
	 * Only MCU's mmut is device wide, i.e. no corresponding kctx.
	 */
	kbase_gpu_vm_lock(kctx);

	ret = kbase_mmu_migrate_page(
		as_tagged(page_to_phys(old_page)), as_tagged(page_to_phys(new_page)), old_dma_addr,
		new_dma_addr, PGD_VPFN_LEVEL_GET_LEVEL(page_md->data.pt_mapped.pgd_vpfn_level));

	if (ret == 0) {
		dma_unmap_page(kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
		__ClearPageMovable(old_page);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		ClearPagePrivate(old_page);
		put_page(old_page);

		page_md = kbase_page_private(new_page);
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		__SetPageMovable(new_page, &movable_ops);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
#else
		if (kbdev->mem_migrate.inode->i_mapping) {
			__SetPageMovable(new_page, kbdev->mem_migrate.inode->i_mapping);
			page_md->status = PAGE_MOVABLE_SET(page_md->status);
		}
#endif
		SetPagePrivate(new_page);
		get_page(new_page);
	} else
		dma_unmap_page(kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* Page fault handler for CPU mapping unblocked. */
	kbase_gpu_vm_unlock(kctx);

	return ret;
}

/*
 * kbasep_migrate_page_allocated_mapped - Migrate a memory page that is both
 *                                        allocated and mapped.
 *
 * @old_page:  Page to remove.
 * @new_page:  Page to add.
 *
 * Replace an old page with a new page by migrating its content and all its
 * CPU and GPU mappings. More specifically: the new page shall replace the
 * old page in the MMU page table, as well as in the page array of the physical
 * allocation, which is used to create CPU mappings. Before returning, the new
 * page shall be set as movable and not isolated, while the old page shall lose
 * the movable property.
 */
static int kbasep_migrate_page_allocated_mapped(struct page *old_page, struct page *new_page)
{
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_context *kctx = page_md->data.mapped.mmut->kctx;
	dma_addr_t old_dma_addr, new_dma_addr;
	int ret;

	old_dma_addr = page_md->dma_addr;
	new_dma_addr = dma_map_page(kctx->kbdev->dev, new_page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(kctx->kbdev->dev, new_dma_addr))
		return -ENOMEM;

	/* Lock context to protect access to array of pages in physical allocation.
	 * This blocks the CPU page fault handler from remapping pages.
	 */
	kbase_gpu_vm_lock(kctx);

	/* Unmap the old physical range. */
	unmap_mapping_range(kctx->filp->f_inode->i_mapping, page_md->data.mapped.vpfn << PAGE_SHIFT,
			    PAGE_SIZE, 1);

	ret = kbase_mmu_migrate_page(as_tagged(page_to_phys(old_page)),
				     as_tagged(page_to_phys(new_page)), old_dma_addr, new_dma_addr,
				     MIDGARD_MMU_BOTTOMLEVEL);

	if (ret == 0) {
		dma_unmap_page(kctx->kbdev->dev, old_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

		SetPagePrivate(new_page);
		get_page(new_page);

		/* Clear PG_movable from the old page and release reference. */
		ClearPagePrivate(old_page);
		__ClearPageMovable(old_page);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		put_page(old_page);

		page_md = kbase_page_private(new_page);
		/* Set PG_movable to the new page. */
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		__SetPageMovable(new_page, &movable_ops);
		page_md->status = PAGE_MOVABLE_SET(page_md->status);
#else
		if (kctx->kbdev->mem_migrate.inode->i_mapping) {
			__SetPageMovable(new_page, kctx->kbdev->mem_migrate.inode->i_mapping);
			page_md->status = PAGE_MOVABLE_SET(page_md->status);
		}
#endif
	} else
		dma_unmap_page(kctx->kbdev->dev, new_dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	/* Page fault handler for CPU mapping unblocked. */
	kbase_gpu_vm_unlock(kctx);

	return ret;
}

/**
 * kbase_page_isolate - Isolate a page for migration.
 *
 * @p:    Pointer of the page struct of page to isolate.
 * @mode: LRU Isolation modes.
 *
 * Callback function for Linux to isolate a page and prepare it for migration.
 *
 * Return: true on success, false otherwise.
 */
static bool kbase_page_isolate(struct page *p, isolate_mode_t mode)
{
	bool status_mem_pool = false;
	struct kbase_mem_pool *mem_pool = NULL;
	struct kbase_page_metadata *page_md = kbase_page_private(p);

	CSTD_UNUSED(mode);

	if (!page_md || !IS_PAGE_MOVABLE(page_md->status))
		return false;

	if (!spin_trylock(&page_md->migrate_lock))
		return false;

	if (WARN_ON(IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return false;
	}

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		/* Prepare to remove page from memory pool later only if pool is not
		 * in the process of termination.
		 */
		mem_pool = page_md->data.mem_pool.pool;
		status_mem_pool = true;
		preempt_disable();
		atomic_inc(&mem_pool->isolation_in_progress_cnt);
		break;
	case ALLOCATED_MAPPED:
		/* Mark the page into isolated state, but only if it has no
		 * kernel CPU mappings
		 */
		if (page_md->vmap_count == 0)
			page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
		break;
	case PT_MAPPED:
		/* Mark the page into isolated state. */
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
		break;
	case SPILL_IN_PROGRESS:
	case ALLOCATE_IN_PROGRESS:
	case FREE_IN_PROGRESS:
		break;
	case NOT_MOVABLE:
		/* Opportunistically clear the movable property for these pages */
		__ClearPageMovable(p);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		break;
	default:
		/* State should always fall in one of the previous cases!
		 * Also notice that FREE_ISOLATED_IN_PROGRESS or
		 * FREE_PT_ISOLATED_IN_PROGRESS is impossible because
		 * that state only applies to pages that are already isolated.
		 */
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	/* If the page is still in the memory pool: try to remove it. This will fail
	 * if pool lock is taken which could mean page no longer exists in pool.
	 */
	if (status_mem_pool) {
		if (!spin_trylock(&mem_pool->pool_lock)) {
			atomic_dec(&mem_pool->isolation_in_progress_cnt);
			preempt_enable();
			return false;
		}

		spin_lock(&page_md->migrate_lock);
		/* Check status again to ensure page has not been removed from memory pool. */
		if (PAGE_STATUS_GET(page_md->status) == MEM_POOL) {
			page_md->status = PAGE_ISOLATE_SET(page_md->status, 1);
			list_del_init(&p->lru);
			mem_pool->cur_size--;
		}
		spin_unlock(&page_md->migrate_lock);
		spin_unlock(&mem_pool->pool_lock);
		atomic_dec(&mem_pool->isolation_in_progress_cnt);
		preempt_enable();
	}

	return IS_PAGE_ISOLATED(page_md->status);
}

/**
 * kbase_page_migrate - Migrate content of old page to new page provided.
 *
 * @mapping:  Pointer to address_space struct associated with pages.
 * @new_page: Pointer to the page struct of new page.
 * @old_page: Pointer to the page struct of old page.
 * @mode:     Mode to determine if migration will be synchronised.
 *
 * Callback function for Linux to migrate the content of the old page to the
 * new page provided.
 *
 * Return: 0 on success, error code otherwise.
 */
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
static int kbase_page_migrate(struct address_space *mapping, struct page *new_page,
			      struct page *old_page, enum migrate_mode mode)
#else
static int kbase_page_migrate(struct page *new_page, struct page *old_page, enum migrate_mode mode)
#endif
{
	int err = 0;
	bool status_mem_pool = false;
	bool status_free_pt_isolated_in_progress = false;
	bool status_free_isolated_in_progress = false;
	bool status_pt_mapped = false;
	bool status_mapped = false;
	bool status_not_movable = false;
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_device *kbdev = NULL;

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	CSTD_UNUSED(mapping);
#endif
	CSTD_UNUSED(mode);

	if (!page_md || !IS_PAGE_MOVABLE(page_md->status))
		return -EINVAL;

	if (!spin_trylock(&page_md->migrate_lock))
		return -EAGAIN;

	if (WARN_ON(!IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return -EINVAL;
	}

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		status_mem_pool = true;
		kbdev = page_md->data.mem_pool.kbdev;
		break;
	case ALLOCATED_MAPPED:
		status_mapped = true;
		break;
	case PT_MAPPED:
		status_pt_mapped = true;
		break;
	case FREE_ISOLATED_IN_PROGRESS:
		status_free_isolated_in_progress = true;
		kbdev = page_md->data.free_isolated.kbdev;
		break;
	case FREE_PT_ISOLATED_IN_PROGRESS:
		status_free_pt_isolated_in_progress = true;
		kbdev = page_md->data.free_pt_isolated.kbdev;
		break;
	case NOT_MOVABLE:
		status_not_movable = true;
		break;
	default:
		/* State should always fall in one of the previous cases! */
		err = -EAGAIN;
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	if (status_mem_pool || status_free_isolated_in_progress ||
	    status_free_pt_isolated_in_progress) {
		struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

		kbase_free_page_metadata(kbdev, old_page, NULL);
		__ClearPageMovable(old_page);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
		put_page(old_page);

		/* Just free new page to avoid lock contention. */
		INIT_LIST_HEAD(&new_page->lru);
		get_page(new_page);
		set_page_private(new_page, 0);
		kbase_free_page_later(kbdev, new_page);
		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
	} else if (status_not_movable) {
		err = -EINVAL;
	} else if (status_mapped) {
		err = kbasep_migrate_page_allocated_mapped(old_page, new_page);
	} else if (status_pt_mapped) {
		err = kbasep_migrate_page_pt_mapped(old_page, new_page);
	}

	/* While we want to preserve the movability of pages for which we return
	 * EAGAIN, according to the kernel docs, movable pages for which a critical
	 * error is returned are called putback on, which may not be what we
	 * expect.
	 */
	if (err < 0 && err != -EAGAIN) {
		__ClearPageMovable(old_page);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);
	}

	return err;
}

/**
 * kbase_page_putback - Return isolated page back to kbase.
 *
 * @p: Pointer of the page struct of page.
 *
 * Callback function for Linux to return isolated page back to kbase. This
 * will only be called for a page that has been isolated but failed to
 * migrate. This function will put back the given page to the state it was
 * in before it was isolated.
 */
static void kbase_page_putback(struct page *p)
{
	bool status_mem_pool = false;
	bool status_free_isolated_in_progress = false;
	bool status_free_pt_isolated_in_progress = false;
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	struct kbase_device *kbdev = NULL;

	/* If we don't have page metadata, the page may not belong to the
	 * driver or may already have been freed, and there's nothing we can do
	 */
	if (!page_md)
		return;

	spin_lock(&page_md->migrate_lock);

	if (WARN_ON(!IS_PAGE_ISOLATED(page_md->status))) {
		spin_unlock(&page_md->migrate_lock);
		return;
	}

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		status_mem_pool = true;
		kbdev = page_md->data.mem_pool.kbdev;
		break;
	case ALLOCATED_MAPPED:
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	case PT_MAPPED:
	case NOT_MOVABLE:
		/* Pages should no longer be isolated if they are in a stable state
		 * and used by the driver.
		 */
		page_md->status = PAGE_ISOLATE_SET(page_md->status, 0);
		break;
	case FREE_ISOLATED_IN_PROGRESS:
		status_free_isolated_in_progress = true;
		kbdev = page_md->data.free_isolated.kbdev;
		break;
	case FREE_PT_ISOLATED_IN_PROGRESS:
		status_free_pt_isolated_in_progress = true;
		kbdev = page_md->data.free_pt_isolated.kbdev;
		break;
	default:
		/* State should always fall in one of the previous cases! */
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	/* If page was in a memory pool then just free it to avoid lock contention. The
	 * same is also true to status_free_pt_isolated_in_progress.
	 */
	if (status_mem_pool || status_free_isolated_in_progress ||
	    status_free_pt_isolated_in_progress) {
		__ClearPageMovable(p);
		page_md->status = PAGE_MOVABLE_CLEAR(page_md->status);

		if (!WARN_ON_ONCE(!kbdev)) {
			struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

			kbase_free_page_later(kbdev, p);
			queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
		}
	}
}

#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static const struct movable_operations movable_ops = {
	.isolate_page = kbase_page_isolate,
	.migrate_page = kbase_page_migrate,
	.putback_page = kbase_page_putback,
};
#else
static const struct address_space_operations kbase_address_space_ops = {
	.isolate_page = kbase_page_isolate,
	.migratepage = kbase_page_migrate,
	.putback_page = kbase_page_putback,
};
#endif

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
void kbase_mem_migrate_set_address_space_ops(struct kbase_device *kbdev, struct file *const filp)
{
	mutex_lock(&kbdev->fw_load_lock);

	if (filp) {
		filp->f_inode->i_mapping->a_ops = &kbase_address_space_ops;

		if (!kbdev->mem_migrate.inode) {
			kbdev->mem_migrate.inode = filp->f_inode;
			/* This reference count increment is balanced by iput()
			 * upon termination.
			 */
			atomic_inc(&filp->f_inode->i_count);
		} else {
			WARN_ON(kbdev->mem_migrate.inode != filp->f_inode);
		}
	}

	mutex_unlock(&kbdev->fw_load_lock);
}
#endif

void kbase_mem_migrate_init(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (kbase_page_migration_enabled < 0)
		kbase_page_migration_enabled = 0;

	spin_lock_init(&mem_migrate->free_pages_lock);
	INIT_LIST_HEAD(&mem_migrate->free_pages_list);

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	mem_migrate->inode = NULL;
#endif
	mem_migrate->free_pages_workq =
		alloc_workqueue("free_pages_workq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	INIT_WORK(&mem_migrate->free_pages_work, kbase_free_pages_worker);
}

void kbase_mem_migrate_term(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (mem_migrate->free_pages_workq)
		destroy_workqueue(mem_migrate->free_pages_workq);
#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	iput(mem_migrate->inode);
#endif
}
