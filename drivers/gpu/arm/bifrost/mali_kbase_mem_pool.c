// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2015-2022 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include <linux/mm.h>
#include <linux/migrate.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/shrinker.h>
#include <linux/atomic.h>
#include <linux/version.h>
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
#include <linux/sched/signal.h>
#else
#include <linux/signal.h>
#endif

#define pool_dbg(pool, format, ...) \
	dev_dbg(pool->kbdev->dev, "%s-pool [%zu/%zu]: " format,	\
		(pool->next_pool) ? "kctx" : "kbdev",	\
		kbase_mem_pool_size(pool),	\
		kbase_mem_pool_max_size(pool),	\
		##__VA_ARGS__)

#define NOT_DIRTY false
#define NOT_RECLAIMED false

/**
 * can_alloc_page() - Check if the current thread can allocate a physical page
 *
 * @pool:                Pointer to the memory pool.
 * @page_owner:          Pointer to the task/process that created the Kbase context
 *                       for which a page needs to be allocated. It can be NULL if
 *                       the page won't be associated with Kbase context.
 * @alloc_from_kthread:  Flag indicating that the current thread is a kernel thread.
 *
 * This function checks if the current thread is a kernel thread and can make a
 * request to kernel to allocate a physical page. If the kernel thread is allocating
 * a page for the Kbase context and the process that created the context is exiting
 * or is being killed, then there is no point in doing a page allocation.
 *
 * The check done by the function is particularly helpful when the system is running
 * low on memory. When a page is allocated from the context of a kernel thread, OoM
 * killer doesn't consider the kernel thread for killing and kernel keeps retrying
 * to allocate the page as long as the OoM killer is able to kill processes.
 * The check allows kernel thread to quickly exit the page allocation loop once OoM
 * killer has initiated the killing of @page_owner, thereby unblocking the context
 * termination for @page_owner and freeing of GPU memory allocated by it. This helps
 * in preventing the kernel panic and also limits the number of innocent processes
 * that get killed.
 *
 * Return: true if the page can be allocated otherwise false.
 */
static inline bool can_alloc_page(struct kbase_mem_pool *pool, struct task_struct *page_owner,
				  const bool alloc_from_kthread)
{
	if (likely(!alloc_from_kthread || !page_owner))
		return true;

	if ((page_owner->flags & PF_EXITING) || fatal_signal_pending(page_owner)) {
		dev_info(pool->kbdev->dev, "%s : Process %s/%d exiting",
			__func__, page_owner->comm, task_pid_nr(page_owner));
		return false;
	}

	return true;
}

static size_t kbase_mem_pool_capacity(struct kbase_mem_pool *pool)
{
	ssize_t max_size = kbase_mem_pool_max_size(pool);
	ssize_t cur_size = kbase_mem_pool_size(pool);

	return max(max_size - cur_size, (ssize_t)0);
}

static bool kbase_mem_pool_is_full(struct kbase_mem_pool *pool)
{
	return kbase_mem_pool_size(pool) >= kbase_mem_pool_max_size(pool);
}

static bool kbase_mem_pool_is_empty(struct kbase_mem_pool *pool)
{
	return kbase_mem_pool_size(pool) == 0;
}

static bool set_pool_new_page_metadata(struct kbase_mem_pool *pool, struct page *p,
				       struct list_head *page_list, size_t *list_size)
{
	struct kbase_page_metadata *page_md = kbase_page_private(p);
	bool not_movable = false;

	lockdep_assert_held(&pool->pool_lock);

	/* Free the page instead of adding it to the pool if it's not movable.
	 * Only update page status and add the page to the memory pool if
	 * it is not isolated.
	 */
	spin_lock(&page_md->migrate_lock);
	if (PAGE_STATUS_GET(page_md->status) == (u8)NOT_MOVABLE) {
		not_movable = true;
	} else if (!WARN_ON_ONCE(IS_PAGE_ISOLATED(page_md->status))) {
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)MEM_POOL);
		page_md->data.mem_pool.pool = pool;
		page_md->data.mem_pool.kbdev = pool->kbdev;
		list_add(&p->lru, page_list);
		(*list_size)++;
	}
	spin_unlock(&page_md->migrate_lock);

	if (not_movable) {
		kbase_free_page_later(pool->kbdev, p);
		pool_dbg(pool, "skipping a not movable page\n");
	}

	return not_movable;
}

static void kbase_mem_pool_add_locked(struct kbase_mem_pool *pool,
		struct page *p)
{
	bool queue_work_to_free = false;

	lockdep_assert_held(&pool->pool_lock);

	if (!pool->order && kbase_page_migration_enabled) {
		if (set_pool_new_page_metadata(pool, p, &pool->page_list, &pool->cur_size))
			queue_work_to_free = true;
	} else {
		list_add(&p->lru, &pool->page_list);
		pool->cur_size++;
	}

	if (queue_work_to_free) {
		struct kbase_mem_migrate *mem_migrate = &pool->kbdev->mem_migrate;

		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
	}

	pool_dbg(pool, "added page\n");
}

static void kbase_mem_pool_add(struct kbase_mem_pool *pool, struct page *p)
{
	kbase_mem_pool_lock(pool);
	kbase_mem_pool_add_locked(pool, p);
	kbase_mem_pool_unlock(pool);
}

static void kbase_mem_pool_add_list_locked(struct kbase_mem_pool *pool,
		struct list_head *page_list, size_t nr_pages)
{
	bool queue_work_to_free = false;

	lockdep_assert_held(&pool->pool_lock);

	if (!pool->order && kbase_page_migration_enabled) {
		struct page *p, *tmp;

		list_for_each_entry_safe(p, tmp, page_list, lru) {
			list_del_init(&p->lru);
			if (set_pool_new_page_metadata(pool, p, &pool->page_list, &pool->cur_size))
				queue_work_to_free = true;
		}
	} else {
		list_splice(page_list, &pool->page_list);
		pool->cur_size += nr_pages;
	}

	if (queue_work_to_free) {
		struct kbase_mem_migrate *mem_migrate = &pool->kbdev->mem_migrate;

		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
	}

	pool_dbg(pool, "added %zu pages\n", nr_pages);
}

static void kbase_mem_pool_add_list(struct kbase_mem_pool *pool,
		struct list_head *page_list, size_t nr_pages)
{
	kbase_mem_pool_lock(pool);
	kbase_mem_pool_add_list_locked(pool, page_list, nr_pages);
	kbase_mem_pool_unlock(pool);
}

static struct page *kbase_mem_pool_remove_locked(struct kbase_mem_pool *pool,
						 enum kbase_page_status status)
{
	struct page *p;

	lockdep_assert_held(&pool->pool_lock);

	if (kbase_mem_pool_is_empty(pool))
		return NULL;

	p = list_first_entry(&pool->page_list, struct page, lru);

	if (!pool->order && kbase_page_migration_enabled) {
		struct kbase_page_metadata *page_md = kbase_page_private(p);

		spin_lock(&page_md->migrate_lock);
		WARN_ON(PAGE_STATUS_GET(page_md->status) != (u8)MEM_POOL);
		page_md->status = PAGE_STATUS_SET(page_md->status, (u8)status);
		spin_unlock(&page_md->migrate_lock);
	}

	list_del_init(&p->lru);
	pool->cur_size--;

	pool_dbg(pool, "removed page\n");

	return p;
}

static struct page *kbase_mem_pool_remove(struct kbase_mem_pool *pool,
					  enum kbase_page_status status)
{
	struct page *p;

	kbase_mem_pool_lock(pool);
	p = kbase_mem_pool_remove_locked(pool, status);
	kbase_mem_pool_unlock(pool);

	return p;
}

static void kbase_mem_pool_sync_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	struct device *dev = pool->kbdev->dev;
	dma_addr_t dma_addr = pool->order ? kbase_dma_addr_as_priv(p) : kbase_dma_addr(p);

	dma_sync_single_for_device(dev, dma_addr, (PAGE_SIZE << pool->order), DMA_BIDIRECTIONAL);
}

static void kbase_mem_pool_zero_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	int i;

	for (i = 0; i < (1U << pool->order); i++)
		clear_highpage(p+i);

	kbase_mem_pool_sync_page(pool, p);
}

static void kbase_mem_pool_spill(struct kbase_mem_pool *next_pool,
		struct page *p)
{
	/* Zero page before spilling */
	kbase_mem_pool_zero_page(next_pool, p);

	kbase_mem_pool_add(next_pool, p);
}

struct page *kbase_mem_alloc_page(struct kbase_mem_pool *pool)
{
	struct page *p;
	gfp_t gfp = __GFP_ZERO;
	struct kbase_device *const kbdev = pool->kbdev;
	struct device *const dev = kbdev->dev;
	dma_addr_t dma_addr;
	int i;

	/* don't warn on higher order failures */
	if (pool->order)
		gfp |= GFP_HIGHUSER | __GFP_NOWARN;
	else
		gfp |= kbase_page_migration_enabled ? GFP_HIGHUSER_MOVABLE : GFP_HIGHUSER;

	p = kbdev->mgm_dev->ops.mgm_alloc_page(kbdev->mgm_dev,
		pool->group_id, gfp, pool->order);
	if (!p)
		return NULL;

	dma_addr = dma_map_page(dev, p, 0, (PAGE_SIZE << pool->order),
				DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, dma_addr)) {
		kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev,
			pool->group_id, p, pool->order);
		return NULL;
	}

	/* Setup page metadata for 4KB pages when page migration is enabled */
	if (!pool->order && kbase_page_migration_enabled) {
		INIT_LIST_HEAD(&p->lru);
		if (!kbase_alloc_page_metadata(kbdev, p, dma_addr, pool->group_id)) {
			dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
			kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev, pool->group_id, p,
							  pool->order);
			return NULL;
		}
	} else {
		WARN_ON(dma_addr != page_to_phys(p));
		for (i = 0; i < (1u << pool->order); i++)
			kbase_set_dma_addr_as_priv(p + i, dma_addr + PAGE_SIZE * i);
	}

	return p;
}

static void enqueue_free_pool_pages_work(struct kbase_mem_pool *pool)
{
	struct kbase_mem_migrate *mem_migrate = &pool->kbdev->mem_migrate;

	if (!pool->order && kbase_page_migration_enabled)
		queue_work(mem_migrate->free_pages_workq, &mem_migrate->free_pages_work);
}

void kbase_mem_pool_free_page(struct kbase_mem_pool *pool, struct page *p)
{
	struct kbase_device *kbdev;

	if (WARN_ON(!pool))
		return;
	if (WARN_ON(!p))
		return;

	kbdev = pool->kbdev;

	if (!pool->order && kbase_page_migration_enabled) {
		kbase_free_page_later(kbdev, p);
		pool_dbg(pool, "page to be freed to kernel later\n");
	} else {
		int i;
		dma_addr_t dma_addr = kbase_dma_addr_as_priv(p);

		for (i = 0; i < (1u << pool->order); i++)
			kbase_clear_dma_addr_as_priv(p + i);

		dma_unmap_page(kbdev->dev, dma_addr, (PAGE_SIZE << pool->order), DMA_BIDIRECTIONAL);

		kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev, pool->group_id, p, pool->order);

		pool_dbg(pool, "freed page to kernel\n");
	}
}

static size_t kbase_mem_pool_shrink_locked(struct kbase_mem_pool *pool,
		size_t nr_to_shrink)
{
	struct page *p;
	size_t i;

	lockdep_assert_held(&pool->pool_lock);

	for (i = 0; i < nr_to_shrink && !kbase_mem_pool_is_empty(pool); i++) {
		p = kbase_mem_pool_remove_locked(pool, FREE_IN_PROGRESS);
		kbase_mem_pool_free_page(pool, p);
	}

	/* Freeing of pages will be deferred when page migration is enabled. */
	enqueue_free_pool_pages_work(pool);

	return i;
}

static size_t kbase_mem_pool_shrink(struct kbase_mem_pool *pool,
		size_t nr_to_shrink)
{
	size_t nr_freed;

	kbase_mem_pool_lock(pool);
	nr_freed = kbase_mem_pool_shrink_locked(pool, nr_to_shrink);
	kbase_mem_pool_unlock(pool);

	return nr_freed;
}

int kbase_mem_pool_grow(struct kbase_mem_pool *pool, size_t nr_to_grow,
			struct task_struct *page_owner)
{
	struct page *p;
	size_t i;
	const bool alloc_from_kthread = !!(current->flags & PF_KTHREAD);

	kbase_mem_pool_lock(pool);

	pool->dont_reclaim = true;
	for (i = 0; i < nr_to_grow; i++) {
		if (pool->dying) {
			pool->dont_reclaim = false;
			kbase_mem_pool_shrink_locked(pool, nr_to_grow);
			kbase_mem_pool_unlock(pool);

			return -ENOMEM;
		}
		kbase_mem_pool_unlock(pool);

		if (unlikely(!can_alloc_page(pool, page_owner, alloc_from_kthread)))
			return -ENOMEM;

		p = kbase_mem_alloc_page(pool);
		if (!p) {
			kbase_mem_pool_lock(pool);
			pool->dont_reclaim = false;
			kbase_mem_pool_unlock(pool);

			return -ENOMEM;
		}

		kbase_mem_pool_lock(pool);
		kbase_mem_pool_add_locked(pool, p);
	}
	pool->dont_reclaim = false;
	kbase_mem_pool_unlock(pool);

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_mem_pool_grow);

void kbase_mem_pool_trim(struct kbase_mem_pool *pool, size_t new_size)
{
	size_t cur_size;
	int err = 0;

	cur_size = kbase_mem_pool_size(pool);

	if (new_size > pool->max_size)
		new_size = pool->max_size;

	if (new_size < cur_size)
		kbase_mem_pool_shrink(pool, cur_size - new_size);
	else if (new_size > cur_size)
		err = kbase_mem_pool_grow(pool, new_size - cur_size, NULL);

	if (err) {
		size_t grown_size = kbase_mem_pool_size(pool);

		dev_warn(pool->kbdev->dev,
			 "Mem pool not grown to the required size of %zu bytes, grown for additional %zu bytes instead!\n",
			 (new_size - cur_size), (grown_size - cur_size));
	}
}

void kbase_mem_pool_set_max_size(struct kbase_mem_pool *pool, size_t max_size)
{
	size_t cur_size;
	size_t nr_to_shrink;

	kbase_mem_pool_lock(pool);

	pool->max_size = max_size;

	cur_size = kbase_mem_pool_size(pool);
	if (max_size < cur_size) {
		nr_to_shrink = cur_size - max_size;
		kbase_mem_pool_shrink_locked(pool, nr_to_shrink);
	}

	kbase_mem_pool_unlock(pool);
}
KBASE_EXPORT_TEST_API(kbase_mem_pool_set_max_size);

static unsigned long kbase_mem_pool_reclaim_count_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_mem_pool *pool;
	size_t pool_size;

	pool = container_of(s, struct kbase_mem_pool, reclaim);

	kbase_mem_pool_lock(pool);
	if (pool->dont_reclaim && !pool->dying) {
		kbase_mem_pool_unlock(pool);
		/* Tell shrinker to skip reclaim
		 * even though freeable pages are available
		 */
		return 0;
	}
	pool_size = kbase_mem_pool_size(pool);
	kbase_mem_pool_unlock(pool);

	return pool_size;
}

static unsigned long kbase_mem_pool_reclaim_scan_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_mem_pool *pool;
	unsigned long freed;

	pool = container_of(s, struct kbase_mem_pool, reclaim);

	kbase_mem_pool_lock(pool);
	if (pool->dont_reclaim && !pool->dying) {
		kbase_mem_pool_unlock(pool);
		/* Tell shrinker that reclaim can't be made and
		 * do not attempt again for this reclaim context.
		 */
		return SHRINK_STOP;
	}

	pool_dbg(pool, "reclaim scan %ld:\n", sc->nr_to_scan);

	freed = kbase_mem_pool_shrink_locked(pool, sc->nr_to_scan);

	kbase_mem_pool_unlock(pool);

	pool_dbg(pool, "reclaim freed %ld pages\n", freed);

	return freed;
}

int kbase_mem_pool_init(struct kbase_mem_pool *pool, const struct kbase_mem_pool_config *config,
			unsigned int order, int group_id, struct kbase_device *kbdev,
			struct kbase_mem_pool *next_pool)
{
	if (WARN_ON(group_id < 0) ||
		WARN_ON(group_id >= MEMORY_GROUP_MANAGER_NR_GROUPS)) {
		return -EINVAL;
	}

	pool->cur_size = 0;
	pool->max_size = kbase_mem_pool_config_get_max_size(config);
	pool->order = order;
	pool->group_id = group_id;
	pool->kbdev = kbdev;
	pool->next_pool = next_pool;
	pool->dying = false;
	atomic_set(&pool->isolation_in_progress_cnt, 0);

	spin_lock_init(&pool->pool_lock);
	INIT_LIST_HEAD(&pool->page_list);

	pool->reclaim.count_objects = kbase_mem_pool_reclaim_count_objects;
	pool->reclaim.scan_objects = kbase_mem_pool_reclaim_scan_objects;
	pool->reclaim.seeks = DEFAULT_SEEKS;
	/* Kernel versions prior to 3.1 :
	 * struct shrinker does not define batch
	 */
	pool->reclaim.batch = 0;
#if KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE
	register_shrinker(&pool->reclaim);
#else
	register_shrinker(&pool->reclaim, "mali-mem-pool");
#endif

	pool_dbg(pool, "initialized\n");

	return 0;
}
KBASE_EXPORT_TEST_API(kbase_mem_pool_init);

void kbase_mem_pool_mark_dying(struct kbase_mem_pool *pool)
{
	kbase_mem_pool_lock(pool);
	pool->dying = true;
	kbase_mem_pool_unlock(pool);
}

void kbase_mem_pool_term(struct kbase_mem_pool *pool)
{
	struct kbase_mem_pool *next_pool = pool->next_pool;
	struct page *p, *tmp;
	size_t nr_to_spill = 0;
	LIST_HEAD(spill_list);
	LIST_HEAD(free_list);
	int i;

	pool_dbg(pool, "terminate()\n");

	unregister_shrinker(&pool->reclaim);

	kbase_mem_pool_lock(pool);
	pool->max_size = 0;

	if (next_pool && !kbase_mem_pool_is_full(next_pool)) {
		/* Spill to next pool (may overspill) */
		nr_to_spill = kbase_mem_pool_capacity(next_pool);
		nr_to_spill = min(kbase_mem_pool_size(pool), nr_to_spill);

		/* Zero pages first without holding the next_pool lock */
		for (i = 0; i < nr_to_spill; i++) {
			p = kbase_mem_pool_remove_locked(pool, SPILL_IN_PROGRESS);
			if (p)
				list_add(&p->lru, &spill_list);
		}
	}

	while (!kbase_mem_pool_is_empty(pool)) {
		/* Free remaining pages to kernel */
		p = kbase_mem_pool_remove_locked(pool, FREE_IN_PROGRESS);
		if (p)
			list_add(&p->lru, &free_list);
	}

	kbase_mem_pool_unlock(pool);

	if (next_pool && nr_to_spill) {
		list_for_each_entry(p, &spill_list, lru)
			kbase_mem_pool_zero_page(pool, p);

		/* Add new page list to next_pool */
		kbase_mem_pool_add_list(next_pool, &spill_list, nr_to_spill);

		pool_dbg(pool, "terminate() spilled %zu pages\n", nr_to_spill);
	}

	list_for_each_entry_safe(p, tmp, &free_list, lru) {
		list_del_init(&p->lru);
		kbase_mem_pool_free_page(pool, p);
	}

	/* Freeing of pages will be deferred when page migration is enabled. */
	enqueue_free_pool_pages_work(pool);

	/* Before returning wait to make sure there are no pages undergoing page isolation
	 * which will require reference to this pool.
	 */
	while (atomic_read(&pool->isolation_in_progress_cnt))
		cpu_relax();

	pool_dbg(pool, "terminated\n");
}
KBASE_EXPORT_TEST_API(kbase_mem_pool_term);

struct page *kbase_mem_pool_alloc(struct kbase_mem_pool *pool)
{
	struct page *p;

	do {
		pool_dbg(pool, "alloc()\n");
		p = kbase_mem_pool_remove(pool, ALLOCATE_IN_PROGRESS);

		if (p)
			return p;

		pool = pool->next_pool;
	} while (pool);

	return NULL;
}

struct page *kbase_mem_pool_alloc_locked(struct kbase_mem_pool *pool)
{
	lockdep_assert_held(&pool->pool_lock);

	pool_dbg(pool, "alloc_locked()\n");
	return kbase_mem_pool_remove_locked(pool, ALLOCATE_IN_PROGRESS);
}

void kbase_mem_pool_free(struct kbase_mem_pool *pool, struct page *p,
		bool dirty)
{
	struct kbase_mem_pool *next_pool = pool->next_pool;

	pool_dbg(pool, "free()\n");

	if (!kbase_mem_pool_is_full(pool)) {
		/* Add to our own pool */
		if (dirty)
			kbase_mem_pool_sync_page(pool, p);

		kbase_mem_pool_add(pool, p);
	} else if (next_pool && !kbase_mem_pool_is_full(next_pool)) {
		/* Spill to next pool */
		kbase_mem_pool_spill(next_pool, p);
	} else {
		/* Free page */
		kbase_mem_pool_free_page(pool, p);
		/* Freeing of pages will be deferred when page migration is enabled. */
		enqueue_free_pool_pages_work(pool);
	}
}

void kbase_mem_pool_free_locked(struct kbase_mem_pool *pool, struct page *p,
		bool dirty)
{
	pool_dbg(pool, "free_locked()\n");

	lockdep_assert_held(&pool->pool_lock);

	if (!kbase_mem_pool_is_full(pool)) {
		/* Add to our own pool */
		if (dirty)
			kbase_mem_pool_sync_page(pool, p);

		kbase_mem_pool_add_locked(pool, p);
	} else {
		/* Free page */
		kbase_mem_pool_free_page(pool, p);
		/* Freeing of pages will be deferred when page migration is enabled. */
		enqueue_free_pool_pages_work(pool);
	}
}

int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_4k_pages,
			       struct tagged_addr *pages, bool partial_allowed,
			       struct task_struct *page_owner)
{
	struct page *p;
	size_t nr_from_pool;
	size_t i = 0;
	int err = -ENOMEM;
	size_t nr_pages_internal;
	const bool alloc_from_kthread = !!(current->flags & PF_KTHREAD);

	nr_pages_internal = nr_4k_pages / (1u << (pool->order));

	if (nr_pages_internal * (1u << pool->order) != nr_4k_pages)
		return -EINVAL;

	pool_dbg(pool, "alloc_pages(4k=%zu):\n", nr_4k_pages);
	pool_dbg(pool, "alloc_pages(internal=%zu):\n", nr_pages_internal);

	/* Get pages from this pool */
	kbase_mem_pool_lock(pool);
	nr_from_pool = min(nr_pages_internal, kbase_mem_pool_size(pool));

	while (nr_from_pool--) {
		int j;

		p = kbase_mem_pool_remove_locked(pool, ALLOCATE_IN_PROGRESS);

		if (pool->order) {
			pages[i++] = as_tagged_tag(page_to_phys(p),
						   HUGE_HEAD | HUGE_PAGE);
			for (j = 1; j < (1u << pool->order); j++)
				pages[i++] = as_tagged_tag(page_to_phys(p) +
							   PAGE_SIZE * j,
							   HUGE_PAGE);
		} else {
			pages[i++] = as_tagged(page_to_phys(p));
		}
	}
	kbase_mem_pool_unlock(pool);

	if (i != nr_4k_pages && pool->next_pool) {
		/* Allocate via next pool */
		err = kbase_mem_pool_alloc_pages(pool->next_pool, nr_4k_pages - i, pages + i,
						 partial_allowed, page_owner);

		if (err < 0)
			goto err_rollback;

		i += err;
	} else {
		/* Get any remaining pages from kernel */
		while (i != nr_4k_pages) {
			if (unlikely(!can_alloc_page(pool, page_owner, alloc_from_kthread)))
				goto err_rollback;

			p = kbase_mem_alloc_page(pool);
			if (!p) {
				if (partial_allowed)
					goto done;
				else
					goto err_rollback;
			}

			if (pool->order) {
				int j;

				pages[i++] = as_tagged_tag(page_to_phys(p),
							   HUGE_PAGE |
							   HUGE_HEAD);
				for (j = 1; j < (1u << pool->order); j++) {
					phys_addr_t phys;

					phys = page_to_phys(p) + PAGE_SIZE * j;
					pages[i++] = as_tagged_tag(phys,
								   HUGE_PAGE);
				}
			} else {
				pages[i++] = as_tagged(page_to_phys(p));
			}
		}
	}

done:
	pool_dbg(pool, "alloc_pages(%zu) done\n", i);
	return i;

err_rollback:
	kbase_mem_pool_free_pages(pool, i, pages, NOT_DIRTY, NOT_RECLAIMED);
	return err;
}

int kbase_mem_pool_alloc_pages_locked(struct kbase_mem_pool *pool,
		size_t nr_4k_pages, struct tagged_addr *pages)
{
	struct page *p;
	size_t i;
	size_t nr_pages_internal;

	lockdep_assert_held(&pool->pool_lock);

	nr_pages_internal = nr_4k_pages / (1u << (pool->order));

	if (nr_pages_internal * (1u << pool->order) != nr_4k_pages)
		return -EINVAL;

	pool_dbg(pool, "alloc_pages_locked(4k=%zu):\n", nr_4k_pages);
	pool_dbg(pool, "alloc_pages_locked(internal=%zu):\n",
			nr_pages_internal);

	if (kbase_mem_pool_size(pool) < nr_pages_internal) {
		pool_dbg(pool, "Failed alloc\n");
		return -ENOMEM;
	}

	for (i = 0; i < nr_pages_internal; i++) {
		int j;

		p = kbase_mem_pool_remove_locked(pool, ALLOCATE_IN_PROGRESS);
		if (pool->order) {
			*pages++ = as_tagged_tag(page_to_phys(p),
						   HUGE_HEAD | HUGE_PAGE);
			for (j = 1; j < (1u << pool->order); j++) {
				*pages++ = as_tagged_tag(page_to_phys(p) +
							   PAGE_SIZE * j,
							   HUGE_PAGE);
			}
		} else {
			*pages++ = as_tagged(page_to_phys(p));
		}
	}

	return nr_4k_pages;
}

static void kbase_mem_pool_add_array(struct kbase_mem_pool *pool,
				     size_t nr_pages, struct tagged_addr *pages,
				     bool zero, bool sync)
{
	struct page *p;
	size_t nr_to_pool = 0;
	LIST_HEAD(new_page_list);
	size_t i;

	if (!nr_pages)
		return;

	pool_dbg(pool, "add_array(%zu, zero=%d, sync=%d):\n",
			nr_pages, zero, sync);

	/* Zero/sync pages first without holding the pool lock */
	for (i = 0; i < nr_pages; i++) {
		if (unlikely(!as_phys_addr_t(pages[i])))
			continue;

		if (is_huge_head(pages[i]) || !is_huge(pages[i])) {
			p = as_page(pages[i]);
			if (zero)
				kbase_mem_pool_zero_page(pool, p);
			else if (sync)
				kbase_mem_pool_sync_page(pool, p);

			list_add(&p->lru, &new_page_list);
			nr_to_pool++;
		}
		pages[i] = as_tagged(0);
	}

	/* Add new page list to pool */
	kbase_mem_pool_add_list(pool, &new_page_list, nr_to_pool);

	pool_dbg(pool, "add_array(%zu) added %zu pages\n",
			nr_pages, nr_to_pool);
}

static void kbase_mem_pool_add_array_locked(struct kbase_mem_pool *pool,
		size_t nr_pages, struct tagged_addr *pages,
		bool zero, bool sync)
{
	struct page *p;
	size_t nr_to_pool = 0;
	LIST_HEAD(new_page_list);
	size_t i;

	lockdep_assert_held(&pool->pool_lock);

	if (!nr_pages)
		return;

	pool_dbg(pool, "add_array_locked(%zu, zero=%d, sync=%d):\n",
			nr_pages, zero, sync);

	/* Zero/sync pages first */
	for (i = 0; i < nr_pages; i++) {
		if (unlikely(!as_phys_addr_t(pages[i])))
			continue;

		if (is_huge_head(pages[i]) || !is_huge(pages[i])) {
			p = as_page(pages[i]);
			if (zero)
				kbase_mem_pool_zero_page(pool, p);
			else if (sync)
				kbase_mem_pool_sync_page(pool, p);

			list_add(&p->lru, &new_page_list);
			nr_to_pool++;
		}
		pages[i] = as_tagged(0);
	}

	/* Add new page list to pool */
	kbase_mem_pool_add_list_locked(pool, &new_page_list, nr_to_pool);

	pool_dbg(pool, "add_array_locked(%zu) added %zu pages\n",
			nr_pages, nr_to_pool);
}

void kbase_mem_pool_free_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		struct tagged_addr *pages, bool dirty, bool reclaimed)
{
	struct kbase_mem_pool *next_pool = pool->next_pool;
	struct page *p;
	size_t nr_to_pool;
	LIST_HEAD(to_pool_list);
	size_t i = 0;
	bool pages_released = false;

	pool_dbg(pool, "free_pages(%zu):\n", nr_pages);

	if (!reclaimed) {
		/* Add to this pool */
		nr_to_pool = kbase_mem_pool_capacity(pool);
		nr_to_pool = min(nr_pages, nr_to_pool);

		kbase_mem_pool_add_array(pool, nr_to_pool, pages, false, dirty);

		i += nr_to_pool;

		if (i != nr_pages && next_pool) {
			/* Spill to next pool (may overspill) */
			nr_to_pool = kbase_mem_pool_capacity(next_pool);
			nr_to_pool = min(nr_pages - i, nr_to_pool);

			kbase_mem_pool_add_array(next_pool, nr_to_pool,
					pages + i, true, dirty);
			i += nr_to_pool;
		}
	}

	/* Free any remaining pages to kernel */
	for (; i < nr_pages; i++) {
		if (unlikely(!as_phys_addr_t(pages[i])))
			continue;

		if (is_huge(pages[i]) && !is_huge_head(pages[i])) {
			pages[i] = as_tagged(0);
			continue;
		}
		p = as_page(pages[i]);

		kbase_mem_pool_free_page(pool, p);
		pages[i] = as_tagged(0);
		pages_released = true;
	}

	/* Freeing of pages will be deferred when page migration is enabled. */
	if (pages_released)
		enqueue_free_pool_pages_work(pool);

	pool_dbg(pool, "free_pages(%zu) done\n", nr_pages);
}


void kbase_mem_pool_free_pages_locked(struct kbase_mem_pool *pool,
		size_t nr_pages, struct tagged_addr *pages, bool dirty,
		bool reclaimed)
{
	struct page *p;
	size_t nr_to_pool;
	LIST_HEAD(to_pool_list);
	size_t i = 0;
	bool pages_released = false;

	lockdep_assert_held(&pool->pool_lock);

	pool_dbg(pool, "free_pages_locked(%zu):\n", nr_pages);

	if (!reclaimed) {
		/* Add to this pool */
		nr_to_pool = kbase_mem_pool_capacity(pool);
		nr_to_pool = min(nr_pages, nr_to_pool);

		kbase_mem_pool_add_array_locked(pool, nr_to_pool, pages, false,
						dirty);

		i += nr_to_pool;
	}

	/* Free any remaining pages to kernel */
	for (; i < nr_pages; i++) {
		if (unlikely(!as_phys_addr_t(pages[i])))
			continue;

		if (is_huge(pages[i]) && !is_huge_head(pages[i])) {
			pages[i] = as_tagged(0);
			continue;
		}

		p = as_page(pages[i]);

		kbase_mem_pool_free_page(pool, p);
		pages[i] = as_tagged(0);
		pages_released = true;
	}

	/* Freeing of pages will be deferred when page migration is enabled. */
	if (pages_released)
		enqueue_free_pool_pages_work(pool);

	pool_dbg(pool, "free_pages_locked(%zu) done\n", nr_pages);
}
