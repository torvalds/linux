// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

/* Global integer used to determine if module parameter value has been
 * provided and if page migration feature is enabled.
 * Feature is disabled on all platforms by default.
 */
int kbase_page_migration_enabled;
module_param(kbase_page_migration_enabled, int, 0444);
KBASE_EXPORT_TEST_API(kbase_page_migration_enabled);

bool kbase_alloc_page_metadata(struct kbase_device *kbdev, struct page *p, dma_addr_t dma_addr)
{
	struct kbase_page_metadata *page_md =
		kzalloc(sizeof(struct kbase_page_metadata), GFP_KERNEL);

	if (!page_md)
		return false;

	SetPagePrivate(p);
	set_page_private(p, (unsigned long)page_md);
	page_md->dma_addr = dma_addr;
	page_md->status = PAGE_STATUS_SET(page_md->status, (u8)ALLOCATE_IN_PROGRESS);
	spin_lock_init(&page_md->migrate_lock);

	lock_page(p);
	if (kbdev->mem_migrate.mapping)
		__SetPageMovable(p, kbdev->mem_migrate.mapping);
	unlock_page(p);

	return true;
}

static void kbase_free_page_metadata(struct kbase_device *kbdev, struct page *p)
{
	struct device *const dev = kbdev->dev;
	struct kbase_page_metadata *page_md;
	dma_addr_t dma_addr;

	page_md = kbase_page_private(p);
	if (!page_md)
		return;

	dma_addr = kbase_dma_addr(p);
	dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);

	kfree(page_md);
	ClearPagePrivate(p);
}

static void kbase_free_pages_worker(struct work_struct *work)
{
	struct kbase_mem_migrate *mem_migrate =
		container_of(work, struct kbase_mem_migrate, free_pages_work);
	struct kbase_device *kbdev = container_of(mem_migrate, struct kbase_device, mem_migrate);
	struct page *p, *tmp;
	LIST_HEAD(free_list);

	spin_lock(&mem_migrate->free_pages_lock);
	list_splice_init(&mem_migrate->free_pages_list, &free_list);
	spin_unlock(&mem_migrate->free_pages_lock);

	list_for_each_entry_safe(p, tmp, &free_list, lru) {
		list_del_init(&p->lru);

		lock_page(p);
		if (PageMovable(p))
			__ClearPageMovable(p);
		unlock_page(p);

		kbase_free_page_metadata(kbdev, p);
		__free_pages(p, 0);
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
	case PT_MAPPED:
		/* Only pages in a memory pool can be isolated for now. */
		break;
	case SPILL_IN_PROGRESS:
	case ALLOCATE_IN_PROGRESS:
	case FREE_IN_PROGRESS:
		/* Transitory state: do nothing. */
		break;
	default:
		/* State should always fall in one of the previous cases!
		 * Also notice that FREE_ISOLATED_IN_PROGRESS is impossible because
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
static int kbase_page_migrate(struct address_space *mapping, struct page *new_page,
			      struct page *old_page, enum migrate_mode mode)
{
	int err = 0;
	bool status_mem_pool = false;
	struct kbase_page_metadata *page_md = kbase_page_private(old_page);
	struct kbase_device *kbdev;

	CSTD_UNUSED(mapping);
	CSTD_UNUSED(mode);

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
	case PT_MAPPED:
	case FREE_ISOLATED_IN_PROGRESS:
	case MULTI_MAPPED:
		/* So far, only pages in a memory pool can be migrated. */
	default:
		/* State should always fall in one of the previous cases! */
		err = -EAGAIN;
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	if (status_mem_pool) {
		struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

		kbase_free_page_metadata(kbdev, old_page);
		__ClearPageMovable(old_page);

		/* Just free new page to avoid lock contention. */
		INIT_LIST_HEAD(&new_page->lru);
		set_page_private(new_page, 0);
		kbase_free_page_later(kbdev, new_page);
		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
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
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	struct kbase_device *kbdev;

	spin_lock(&page_md->migrate_lock);

	/* Page must have been isolated to reach here but metadata is incorrect. */
	WARN_ON(!IS_PAGE_ISOLATED(page_md->status));

	switch (PAGE_STATUS_GET(page_md->status)) {
	case MEM_POOL:
		status_mem_pool = true;
		kbdev = page_md->data.mem_pool.kbdev;
		break;
	case ALLOCATED_MAPPED:
	case PT_MAPPED:
	case FREE_ISOLATED_IN_PROGRESS:
		/* Only pages in a memory pool can be isolated for now.
		 * Therefore only pages in a memory pool can be 'putback'.
		 */
		break;
	default:
		/* State should always fall in one of the previous cases! */
		break;
	}

	spin_unlock(&page_md->migrate_lock);

	/* If page was in a memory pool then just free it to avoid lock contention. */
	if (!WARN_ON(!status_mem_pool)) {
		struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

		__ClearPageMovable(p);
		list_del_init(&p->lru);
		kbase_free_page_later(kbdev, p);
		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
	}
}

static const struct address_space_operations kbase_address_space_ops = {
	.isolate_page = kbase_page_isolate,
	.migratepage = kbase_page_migrate,
	.putback_page = kbase_page_putback,
};

void kbase_mem_migrate_set_address_space_ops(struct kbase_device *kbdev, struct file *const filp)
{
	if (filp) {
		filp->f_inode->i_mapping->a_ops = &kbase_address_space_ops;

		if (!kbdev->mem_migrate.mapping)
			kbdev->mem_migrate.mapping = filp->f_inode->i_mapping;
		else
			WARN_ON(kbdev->mem_migrate.mapping != filp->f_inode->i_mapping);
	}
}

void kbase_mem_migrate_init(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (kbase_page_migration_enabled < 0)
		kbase_page_migration_enabled = 0;

	spin_lock_init(&mem_migrate->free_pages_lock);
	INIT_LIST_HEAD(&mem_migrate->free_pages_list);

	mem_migrate->free_pages_workq =
		alloc_workqueue("free_pages_workq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	INIT_WORK(&mem_migrate->free_pages_work, kbase_free_pages_worker);
}

void kbase_mem_migrate_term(struct kbase_device *kbdev)
{
	struct kbase_mem_migrate *mem_migrate = &kbdev->mem_migrate;

	if (mem_migrate->free_pages_workq)
		destroy_workqueue(mem_migrate->free_pages_workq);
}
