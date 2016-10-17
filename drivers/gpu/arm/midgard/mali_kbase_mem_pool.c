/*
 *
 * (C) COPYRIGHT 2015-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
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

/* This function is only provided for backwards compatibility with kernels
 * which use the old carveout allocator.
 *
 * The forward declaration is to keep sparse happy.
 */
int __init kbase_carveout_mem_reserve(
		phys_addr_t size);
int __init kbase_carveout_mem_reserve(phys_addr_t size)
{
	return 0;
}

#define pool_dbg(pool, format, ...) \
	dev_dbg(pool->kbdev->dev, "%s-pool [%zu/%zu]: " format,	\
		(pool->next_pool) ? "kctx" : "kbdev",	\
		kbase_mem_pool_size(pool),	\
		kbase_mem_pool_max_size(pool),	\
		##__VA_ARGS__)

#define NOT_DIRTY false
#define NOT_RECLAIMED false

static inline void kbase_mem_pool_lock(struct kbase_mem_pool *pool)
{
	spin_lock(&pool->pool_lock);
}

static inline void kbase_mem_pool_unlock(struct kbase_mem_pool *pool)
{
	spin_unlock(&pool->pool_lock);
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

static void kbase_mem_pool_add_locked(struct kbase_mem_pool *pool,
		struct page *p)
{
	lockdep_assert_held(&pool->pool_lock);

	list_add(&p->lru, &pool->page_list);
	pool->cur_size++;

	zone_page_state_add(1, page_zone(p), NR_SLAB_RECLAIMABLE);

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
	struct page *p;

	lockdep_assert_held(&pool->pool_lock);

	list_for_each_entry(p, page_list, lru) {
		zone_page_state_add(1, page_zone(p), NR_SLAB_RECLAIMABLE);
	}

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

	zone_page_state_add(-1, page_zone(p), NR_SLAB_RECLAIMABLE);

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
			PAGE_SIZE, DMA_BIDIRECTIONAL);
}

static void kbase_mem_pool_zero_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	clear_highpage(p);
	kbase_mem_pool_sync_page(pool, p);
}

static void kbase_mem_pool_spill(struct kbase_mem_pool *next_pool,
		struct page *p)
{
	/* Zero page before spilling */
	kbase_mem_pool_zero_page(next_pool, p);

	kbase_mem_pool_add(next_pool, p);
}

struct page *kbase_mem_alloc_page(struct kbase_device *kbdev)
{
	struct page *p;
	gfp_t gfp;
	struct device *dev = kbdev->dev;
	dma_addr_t dma_addr;

#if defined(CONFIG_ARM) && !defined(CONFIG_HAVE_DMA_ATTRS) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
	/* DMA cache sync fails for HIGHMEM before 3.5 on ARM */
	gfp = GFP_USER | __GFP_ZERO;
#else
	gfp = GFP_HIGHUSER | __GFP_ZERO;
#endif

	if (current->flags & PF_KTHREAD) {
		/* Don't trigger OOM killer from kernel threads, e.g. when
		 * growing memory on GPU page fault */
		gfp |= __GFP_NORETRY;
	}

	p = alloc_page(gfp);
	if (!p)
		return NULL;

	dma_addr = dma_map_page(dev, p, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, dma_addr)) {
		__free_page(p);
		return NULL;
	}

	WARN_ON(dma_addr != page_to_phys(p));

	kbase_set_dma_addr(p, dma_addr);

	return p;
}

static void kbase_mem_pool_free_page(struct kbase_mem_pool *pool,
		struct page *p)
{
	struct device *dev = pool->kbdev->dev;
	dma_addr_t dma_addr = kbase_dma_addr(p);

	dma_unmap_page(dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	kbase_clear_dma_addr(p);
	__free_page(p);

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

	for (i = 0; i < nr_to_grow; i++) {
		p = kbase_mem_alloc_page(pool->kbdev);
		if (!p)
			return -ENOMEM;
		kbase_mem_pool_add(pool, p);
	}

	return 0;
}

void kbase_mem_pool_trim(struct kbase_mem_pool *pool, size_t new_size)
{
	size_t cur_size;

	cur_size = kbase_mem_pool_size(pool);

	if (new_size > pool->max_size)
		new_size = pool->max_size;

	if (new_size < cur_size)
		kbase_mem_pool_shrink(pool, cur_size - new_size);
	else if (new_size > cur_size)
		kbase_mem_pool_grow(pool, new_size - cur_size);
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


static unsigned long kbase_mem_pool_reclaim_count_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_mem_pool *pool;

	pool = container_of(s, struct kbase_mem_pool, reclaim);
	pool_dbg(pool, "reclaim count: %zu\n", kbase_mem_pool_size(pool));
	return kbase_mem_pool_size(pool);
}

static unsigned long kbase_mem_pool_reclaim_scan_objects(struct shrinker *s,
		struct shrink_control *sc)
{
	struct kbase_mem_pool *pool;
	unsigned long freed;

	pool = container_of(s, struct kbase_mem_pool, reclaim);

	pool_dbg(pool, "reclaim scan %ld:\n", sc->nr_to_scan);

	freed = kbase_mem_pool_shrink(pool, sc->nr_to_scan);

	pool_dbg(pool, "reclaim freed %ld pages\n", freed);

	return freed;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
static int kbase_mem_pool_reclaim_shrink(struct shrinker *s,
		struct shrink_control *sc)
{
	if (sc->nr_to_scan == 0)
		return kbase_mem_pool_reclaim_count_objects(s, sc);

	return kbase_mem_pool_reclaim_scan_objects(s, sc);
}
#endif

int kbase_mem_pool_init(struct kbase_mem_pool *pool,
		size_t max_size,
		struct kbase_device *kbdev,
		struct kbase_mem_pool *next_pool)
{
	pool->cur_size = 0;
	pool->max_size = max_size;
	pool->kbdev = kbdev;
	pool->next_pool = next_pool;

	spin_lock_init(&pool->pool_lock);
	INIT_LIST_HEAD(&pool->page_list);

	/* Register shrinker */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	pool->reclaim.shrink = kbase_mem_pool_reclaim_shrink;
#else
	pool->reclaim.count_objects = kbase_mem_pool_reclaim_count_objects;
	pool->reclaim.scan_objects = kbase_mem_pool_reclaim_scan_objects;
#endif
	pool->reclaim.seeks = DEFAULT_SEEKS;
	/* Kernel versions prior to 3.1 :
	 * struct shrinker does not define batch */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	pool->reclaim.batch = 0;
#endif
	register_shrinker(&pool->reclaim);

	pool_dbg(pool, "initialized\n");

	return 0;
}

void kbase_mem_pool_term(struct kbase_mem_pool *pool)
{
	struct kbase_mem_pool *next_pool = pool->next_pool;
	struct page *p;
	size_t nr_to_spill = 0;
	LIST_HEAD(spill_list);
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
			kbase_mem_pool_zero_page(pool, p);
			list_add(&p->lru, &spill_list);
		}
	}

	while (!kbase_mem_pool_is_empty(pool)) {
		/* Free remaining pages to kernel */
		p = kbase_mem_pool_remove_locked(pool);
		kbase_mem_pool_free_page(pool, p);
	}

	kbase_mem_pool_unlock(pool);

	if (next_pool && nr_to_spill) {
		/* Add new page list to next_pool */
		kbase_mem_pool_add_list(next_pool, &spill_list, nr_to_spill);

		pool_dbg(pool, "terminate() spilled %zu pages\n", nr_to_spill);
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

int kbase_mem_pool_alloc_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		phys_addr_t *pages)
{
	struct page *p;
	size_t nr_from_pool;
	size_t i;
	int err = -ENOMEM;

	pool_dbg(pool, "alloc_pages(%zu):\n", nr_pages);

	/* Get pages from this pool */
	kbase_mem_pool_lock(pool);
	nr_from_pool = min(nr_pages, kbase_mem_pool_size(pool));
	for (i = 0; i < nr_from_pool; i++) {
		p = kbase_mem_pool_remove_locked(pool);
		pages[i] = page_to_phys(p);
	}
	kbase_mem_pool_unlock(pool);

	if (i != nr_pages && pool->next_pool) {
		/* Allocate via next pool */
		err = kbase_mem_pool_alloc_pages(pool->next_pool,
				nr_pages - i, pages + i);

		if (err)
			goto err_rollback;

		i += nr_pages - i;
	}

	/* Get any remaining pages from kernel */
	for (; i < nr_pages; i++) {
		p = kbase_mem_alloc_page(pool->kbdev);
		if (!p)
			goto err_rollback;
		pages[i] = page_to_phys(p);
	}

	pool_dbg(pool, "alloc_pages(%zu) done\n", nr_pages);

	return 0;

err_rollback:
	kbase_mem_pool_free_pages(pool, i, pages, NOT_DIRTY, NOT_RECLAIMED);
	return err;
}

static void kbase_mem_pool_add_array(struct kbase_mem_pool *pool,
		size_t nr_pages, phys_addr_t *pages, bool zero, bool sync)
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
		if (unlikely(!pages[i]))
			continue;

		p = phys_to_page(pages[i]);

		if (zero)
			kbase_mem_pool_zero_page(pool, p);
		else if (sync)
			kbase_mem_pool_sync_page(pool, p);

		list_add(&p->lru, &new_page_list);
		nr_to_pool++;
		pages[i] = 0;
	}

	/* Add new page list to pool */
	kbase_mem_pool_add_list(pool, &new_page_list, nr_to_pool);

	pool_dbg(pool, "add_array(%zu) added %zu pages\n",
			nr_pages, nr_to_pool);
}

void kbase_mem_pool_free_pages(struct kbase_mem_pool *pool, size_t nr_pages,
		phys_addr_t *pages, bool dirty, bool reclaimed)
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
		if (unlikely(!pages[i]))
			continue;

		p = phys_to_page(pages[i]);
		if (reclaimed)
			zone_page_state_add(-1, page_zone(p),
					NR_SLAB_RECLAIMABLE);

		kbase_mem_pool_free_page(pool, p);
		pages[i] = 0;
	}

	pool_dbg(pool, "free_pages(%zu) done\n", nr_pages);
}
