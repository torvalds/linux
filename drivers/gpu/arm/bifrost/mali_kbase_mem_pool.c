// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2015-2021 ARM Limited. All rights reserved.
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
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/shrinker.h>
#include <linux/atomic.h>
#include <linux/version.h>

#define pool_dbg(pool, format, ...) \
	dev_dbg(pool->kbdev->dev, "%s-pool [%zu/%zu]: " format,	\
		(pool->next_pool) ? "kctx" : "kbdev",	\
		kbase_mem_pool_size(pool),	\
		kbase_mem_pool_max_size(pool),	\
		##__VA_ARGS__)

#define NOT_DIRTY false
#define NOT_RECLAIMED false

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

static void kbase_mem_pool_add_locked(struct kbase_mem_pool *pool,
		struct page *p)
{
	lockdep_assert_held(&pool->pool_lock);

	list_add(&p->lru, &pool->page_list);
	pool->cur_size++;

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
	lockdep_assert_held(&pool->pool_lock);

	list_splice(page_list, &pool->page_list);
	pool->cur_size += nr_pages;

	pool_dbg(pool, "added %zu pages\n", nr_pages);
}

static void kbase_mem_pool_add_list(struct kbase_mem_pool *pool,
		struct list_head *page_list, size_t nr_pages)
{
	kbase_mem_pool_lock(pool);
	kbase_mem_pool_add_list_locked(pool, page_list, nr_pages);
	kbase_mem_pool_unlock(pool);
}

static struct page *kbase_mem_pool_remove_locked(struct kbase_mem_pool *pool)
{
	struct page *p;

	lockdep_assert_held(&pool->pool_lock);

	if (kbase_mem_pool_is_empty(pool))
		return NULL;

	p = list_first_entry(&pool->page_list, struct page, lru);
	list_del_init(&p->lru);
	pool->cur_size--;

	pool_dbg(pool, "removed page\n");

	return p;
}

static struct page *kbase_mem_pool_remove(struct kbase_mem_pool *pool)
{
	struct page *p;

	kbase_mem_pool_lock(pool);
	p = kbase_mem_pool_remove_locked(pool);
	kbase_mem_pool_unlock(pool);

	return p;
}

static void kbase_mem_pool_sync_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	struct device *dev = pool->kbdev->dev;
	dma_sync_single_for_device(dev, kbase_dma_addr(p),
			(PAGE_SIZE << pool->order), DMA_BIDIRECTIONAL);
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
	gfp_t gfp = GFP_HIGHUSER | __GFP_ZERO;
	struct kbase_device *const kbdev = pool->kbdev;
	struct device *const dev = kbdev->dev;
	dma_addr_t dma_addr;
	int i;

	/* don't warn on higher order failures */
	if (pool->order)
		gfp |= __GFP_NOWARN;

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

	WARN_ON(dma_addr != page_to_phys(p));
	for (i = 0; i < (1u << pool->order); i++)
		kbase_set_dma_addr(p+i, dma_addr + PAGE_SIZE * i);

	return p;
}

static void kbase_mem_pool_free_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	struct kbase_device *const kbdev = pool->kbdev;
	struct device *const dev = kbdev->dev;
	dma_addr_t dma_addr = kbase_dma_addr(p);
	int i;

	dma_unmap_page(dev, dma_addr, (PAGE_SIZE << pool->order),
		       DMA_BIDIRECTIONAL);
	for (i = 0; i < (1u << pool->order); i++)
		kbase_clear_dma_addr(p+i);

	kbdev->mgm_dev->ops.mgm_free_page(kbdev->mgm_dev,
		pool->group_id, p, pool->order);

	pool_dbg(pool, "freed page to kernel\n");
}

static size_t kbase_mem_pool_shrink_locked(struct kbase_mem_pool *pool,
		size_t nr_to_shrink)
{
	struct page *p;
	size_t i;

	lockdep_assert_held(&pool->pool_lock);

	for (i = 0; i < nr_to_shrink && !kbase_mem_pool_is_empty(pool); i++) {
		p = kbase_mem_pool_remove_locked(pool);
		kbase_mem_pool_free_page(pool, p);
	}

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

int kbase_mem_pool_grow(struct kbase_mem_pool *pool,
		size_t nr_to_grow)
{
	struct page *p;
	size_t i;

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
		err = kbase_mem_pool_grow(pool, new_size - cur_size);

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
		return 0;
	}

	pool_dbg(pool, "reclaim scan %ld:\n", sc->nr_to_scan);

	freed = kbase_mem_pool_shrink_locked(pool, sc->nr_to_scan);

	kbase_mem_pool_unlock(pool);

	pool_dbg(pool, "reclaim freed %ld pages\n", freed);

	return freed;
}

int kbase_mem_pool_init(struct kbase_mem_pool *pool,
		const struct kbase_mem_pool_config *config,
		unsigned int order,
		int group_id,
		struct kbase_device *kbdev,
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

	spin_lock_init(&pool->pool_lock);
	INIT_LIST_HEAD(&pool->page_list);

	pool->reclaim.count_objects = kbase_mem_pool_reclaim_count_objects;
	pool->reclaim.scan_objects = kbase_mem_pool_reclaim_scan_objects;
	pool->reclaim.seeks = DEFAULT_SEEKS;
	/* Kernel versions prior to 3.1 :
	 * struct shrinker does not define batch
	 */
	pool->reclaim.batch = 0;
	register_shrinker(&pool->reclaim);

	pool_dbg(pool, "initialized\n");

	return 0;
}

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
			p = kbase_mem_pool_remove_locked(pool);
			list_add(&p->lru, &spill_list);
		}
	}

	while (!kbase_mem_pool_is_empty(pool)) {
		/* Free remaining pages to kernel */
		p = kbase_mem_pool_remove_locked(pool);
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

	pool_dbg(pool, "terminated\n");
}

struct page *kbase_mem_pool_alloc(struct kbase_mem_pool *pool)
{
	struct page *p;

	do {
		pool_dbg(pool, "alloc()\n");
		p = kbase_mem_pool_remove(pool);

		if (p)
			return p;

		pool = pool->next_pool;
	} while (pool);

	return NULL;
}

struct page *kbase_mem_pool_alloc_locked(struct kbase_mem_pool *pool)
{
	struct page *p;

	lockdep_assert_held(&pool->pool_lock);

	pool_dbg(pool, "alloc_locked()\n");
	p = kbase_mem_pool_remove_locked(pool);

	if (p)
		return p;

	return NULL;
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
	}
}

int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_4k_pages,
		struct tagged_addr *pages, bool partial_allowed)
{
	struct page *p;
	size_t nr_from_pool;
	size_t i = 0;
	int err = -ENOMEM;
	size_t nr_pages_internal;

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
		p = kbase_mem_pool_remove_locked(pool);
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
		err = kbase_mem_pool_alloc_pages(pool->next_pool,
				nr_4k_pages - i, pages + i, partial_allowed);

		if (err < 0)
			goto err_rollback;

		i += err;
	} else {
		/* Get any remaining pages from kernel */
		while (i != nr_4k_pages) {
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

		p = kbase_mem_pool_remove_locked(pool);
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
	}

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
	}

	pool_dbg(pool, "free_pages_locked(%zu) done\n", nr_pages);
}
