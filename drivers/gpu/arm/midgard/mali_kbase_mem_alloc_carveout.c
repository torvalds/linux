/*
 *
 * (C) COPYRIGHT 2010-2015 ARM Limited. All rights reserved.
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



/**
 * @file mali_kbase_mem.c
 * Base kernel memory APIs
 */
#include <mali_kbase.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/mempool.h>
#include <linux/mm.h>
#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/version.h>


/* This code does not support having multiple kbase devices, or rmmod/insmod */

static unsigned long kbase_carveout_start_pfn = ~0UL;
static unsigned long kbase_carveout_end_pfn;
static LIST_HEAD(kbase_carveout_free_list);
static DEFINE_MUTEX(kbase_carveout_free_list_lock);
static unsigned int kbase_carveout_pages;
static atomic_t kbase_carveout_used_pages;
static atomic_t kbase_carveout_system_pages;

static struct page *kbase_carveout_get_page(struct kbase_mem_allocator *allocator)
{
	struct page *p = NULL;
	gfp_t gfp;

	mutex_lock(&kbase_carveout_free_list_lock);
	if (!list_empty(&kbase_carveout_free_list)) {
		p = list_first_entry(&kbase_carveout_free_list, struct page, lru);
		list_del(&p->lru);
		atomic_inc(&kbase_carveout_used_pages);
	}
	mutex_unlock(&kbase_carveout_free_list_lock);

	if (!p) {
		dma_addr_t dma_addr;
#if defined(CONFIG_ARM) && !defined(CONFIG_HAVE_DMA_ATTRS) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
		/* DMA cache sync fails for HIGHMEM before 3.5 on ARM */
		gfp = GFP_USER | __GFP_ZERO;
#else
		gfp = GFP_HIGHUSER | __GFP_ZERO;
#endif

		if (current->flags & PF_KTHREAD) {
			/* Don't trigger OOM killer from kernel threads, e.g.
			 * when growing memory on GPU page fault */
			gfp |= __GFP_NORETRY;
		}

		p = alloc_page(gfp);
		if (!p)
			goto out;

		dma_addr = dma_map_page(allocator->kbdev->dev, p, 0, PAGE_SIZE,
				DMA_BIDIRECTIONAL);
		if (dma_mapping_error(allocator->kbdev->dev, dma_addr)) {
			__free_page(p);
			p = NULL;
			goto out;
		}

		kbase_set_dma_addr(p, dma_addr);
		BUG_ON(dma_addr != PFN_PHYS(page_to_pfn(p)));
		atomic_inc(&kbase_carveout_system_pages);
	}
out:
	return p;
}

static void kbase_carveout_put_page(struct page *p,
				    struct kbase_mem_allocator *allocator)
{
	if (page_to_pfn(p) >= kbase_carveout_start_pfn &&
			page_to_pfn(p) <= kbase_carveout_end_pfn) {
		mutex_lock(&kbase_carveout_free_list_lock);
		list_add(&p->lru, &kbase_carveout_free_list);
		atomic_dec(&kbase_carveout_used_pages);
		mutex_unlock(&kbase_carveout_free_list_lock);
	} else {
		dma_unmap_page(allocator->kbdev->dev, kbase_dma_addr(p),
				PAGE_SIZE,
				DMA_BIDIRECTIONAL);
		ClearPagePrivate(p);
		__free_page(p);
		atomic_dec(&kbase_carveout_system_pages);
	}
}

static int kbase_carveout_seq_show(struct seq_file *s, void *data)
{
	seq_printf(s, "carveout pages: %u\n", kbase_carveout_pages);
	seq_printf(s, "used carveout pages: %u\n",
			atomic_read(&kbase_carveout_used_pages));
	seq_printf(s, "used system pages: %u\n",
			atomic_read(&kbase_carveout_system_pages));
	return 0;
}

static int kbasep_carveout_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, kbase_carveout_seq_show, NULL);
}

static const struct file_operations kbase_carveout_debugfs_fops = {
	.open           = kbasep_carveout_debugfs_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release_private,
};

static int kbase_carveout_init(struct device *dev)
{
	unsigned long pfn;
	static int once;

	mutex_lock(&kbase_carveout_free_list_lock);
	BUG_ON(once);
	once = 1;

	for (pfn = kbase_carveout_start_pfn; pfn <= kbase_carveout_end_pfn; pfn++) {
		struct page *p = pfn_to_page(pfn);
		dma_addr_t dma_addr;

		dma_addr = dma_map_page(dev, p, 0, PAGE_SIZE,
				DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, dma_addr))
			goto out_rollback;

		kbase_set_dma_addr(p, dma_addr);
		BUG_ON(dma_addr != PFN_PHYS(page_to_pfn(p)));

		list_add_tail(&p->lru, &kbase_carveout_free_list);
	}

	mutex_unlock(&kbase_carveout_free_list_lock);

	debugfs_create_file("kbase_carveout", S_IRUGO, NULL, NULL,
		    &kbase_carveout_debugfs_fops);

	return 0;

out_rollback:
	while (!list_empty(&kbase_carveout_free_list)) {
		struct page *p;

		p = list_first_entry(&kbase_carveout_free_list, struct page, lru);
		dma_unmap_page(dev, kbase_dma_addr(p),
				PAGE_SIZE,
				DMA_BIDIRECTIONAL);
		ClearPagePrivate(p);
		list_del(&p->lru);
	}

	mutex_unlock(&kbase_carveout_free_list_lock);
	return -ENOMEM;
}

int __init kbase_carveout_mem_reserve(phys_addr_t size)
{
	phys_addr_t mem;

#if defined(CONFIG_ARM) && !defined(CONFIG_HAVE_DMA_ATTRS) \
		&& LINUX_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
	/* DMA cache sync fails for HIGHMEM before 3.5 on ARM */
	mem = memblock_alloc_base(size, PAGE_SIZE, MEMBLOCK_ALLOC_ACCESSIBLE);
#else
	mem = memblock_alloc_base(size, PAGE_SIZE, MEMBLOCK_ALLOC_ANYWHERE);
#endif
	if (mem == 0) {
		pr_warn("%s: Failed to allocate %d for kbase carveout\n",
				__func__, size);
		return -ENOMEM;
	}

	kbase_carveout_start_pfn = PFN_DOWN(mem);
	kbase_carveout_end_pfn = PFN_DOWN(mem + size - 1);
	kbase_carveout_pages = kbase_carveout_end_pfn - kbase_carveout_start_pfn + 1;

	return 0;
}

int kbase_mem_lowlevel_init(struct kbase_device *kbdev)
{
	return kbase_carveout_init(kbdev->dev);
}

void kbase_mem_lowlevel_term(struct kbase_device *kbdev)
{
}

static int kbase_mem_allocator_shrink(struct shrinker *s, struct shrink_control *sc)
{
	struct kbase_mem_allocator *allocator;
	int i;
	int freed;

	allocator = container_of(s, struct kbase_mem_allocator, free_list_reclaimer);

	if (sc->nr_to_scan == 0)
		return atomic_read(&allocator->free_list_size);

	might_sleep();

	mutex_lock(&allocator->free_list_lock);
	i = MIN(atomic_read(&allocator->free_list_size), sc->nr_to_scan);
	freed = i;

	atomic_sub(i, &allocator->free_list_size);

	while (i--) {
		struct page *p;

		BUG_ON(list_empty(&allocator->free_list_head));
		p = list_first_entry(&allocator->free_list_head, struct page, lru);
		list_del(&p->lru);
		kbase_carveout_put_page(p, allocator);
	}
	mutex_unlock(&allocator->free_list_lock);
	return atomic_read(&allocator->free_list_size);
}

int kbase_mem_allocator_init(struct kbase_mem_allocator * const allocator,
		unsigned int max_size, struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(NULL != allocator);
	KBASE_DEBUG_ASSERT(kbdev);

	INIT_LIST_HEAD(&allocator->free_list_head);

	allocator->kbdev = kbdev;

	mutex_init(&allocator->free_list_lock);

	atomic_set(&allocator->free_list_size, 0);

	allocator->free_list_max_size = max_size;
	allocator->free_list_reclaimer.shrink = kbase_mem_allocator_shrink;
	allocator->free_list_reclaimer.seeks = DEFAULT_SEEKS;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0) /* Kernel versions prior to 3.1 : struct shrinker does not define batch */
	allocator->free_list_reclaimer.batch = 0;
#endif

	register_shrinker(&allocator->free_list_reclaimer);

	return 0;
}

void kbase_mem_allocator_term(struct kbase_mem_allocator *allocator)
{
	KBASE_DEBUG_ASSERT(NULL != allocator);

	unregister_shrinker(&allocator->free_list_reclaimer);

	while (!list_empty(&allocator->free_list_head)) {
		struct page *p;

		p = list_first_entry(&allocator->free_list_head, struct page,
				lru);
		list_del(&p->lru);

		kbase_carveout_put_page(p, allocator);
	}
	mutex_destroy(&allocator->free_list_lock);
}


int kbase_mem_allocator_alloc(struct kbase_mem_allocator *allocator, size_t nr_pages, phys_addr_t *pages)
{
	struct page *p;
	int i;
	int num_from_free_list;
	struct list_head from_free_list = LIST_HEAD_INIT(from_free_list);

	might_sleep();

	KBASE_DEBUG_ASSERT(NULL != allocator);

	/* take from the free list first */
	mutex_lock(&allocator->free_list_lock);
	num_from_free_list = MIN(nr_pages, atomic_read(&allocator->free_list_size));
	atomic_sub(num_from_free_list, &allocator->free_list_size);
	for (i = 0; i < num_from_free_list; i++) {
		BUG_ON(list_empty(&allocator->free_list_head));
		p = list_first_entry(&allocator->free_list_head, struct page, lru);
		list_move(&p->lru, &from_free_list);
	}
	mutex_unlock(&allocator->free_list_lock);
	i = 0;

	/* Allocate as many pages from the pool of already allocated pages. */
	list_for_each_entry(p, &from_free_list, lru) {
		pages[i] = PFN_PHYS(page_to_pfn(p));
		i++;
	}

	if (i == nr_pages)
		return 0;

	/* If not all pages were sourced from the pool, request new ones. */
	for (; i < nr_pages; i++) {
		p = kbase_carveout_get_page(allocator);
		if (NULL == p)
			goto err_out_roll_back;

		kbase_sync_single_for_device(allocator->kbdev,
					   kbase_dma_addr(p),
					   PAGE_SIZE,
					   DMA_BIDIRECTIONAL);

		pages[i] = PFN_PHYS(page_to_pfn(p));
	}

	return 0;

err_out_roll_back:
	while (i--) {
		struct page *p;

		p = pfn_to_page(PFN_DOWN(pages[i]));
		pages[i] = (phys_addr_t)0;
		kbase_carveout_put_page(p, allocator);
	}

	return -ENOMEM;
}

void kbase_mem_allocator_free(struct kbase_mem_allocator *allocator, u32 nr_pages, phys_addr_t *pages, bool sync_back)
{
	int i = 0;
	int page_count = 0;
	int tofree;

	LIST_HEAD(new_free_list_items);

	KBASE_DEBUG_ASSERT(NULL != allocator);

	might_sleep();

	/* Starting by just freeing the overspill.
	* As we do this outside of the lock we might spill too many pages
	* or get too many on the free list, but the max_size is just a ballpark so it is ok
	* providing that tofree doesn't exceed nr_pages
	*/
	tofree = MAX((int)allocator->free_list_max_size - atomic_read(&allocator->free_list_size), 0);
	tofree = nr_pages - MIN(tofree, nr_pages);
	for (; i < tofree; i++) {
		if (likely(0 != pages[i])) {
			struct page *p;

			p = pfn_to_page(PFN_DOWN(pages[i]));
			pages[i] = (phys_addr_t)0;
			kbase_carveout_put_page(p, allocator);
		}
	}

	for (; i < nr_pages; i++) {
		if (likely(0 != pages[i])) {
			struct page *p;

			p = pfn_to_page(PFN_DOWN(pages[i]));
			pages[i] = (phys_addr_t)0;
			/* Sync back the memory to ensure that future cache
			 * invalidations don't trample on memory.
			 */
			if (sync_back)
				kbase_sync_single_for_cpu(allocator->kbdev,
						kbase_dma_addr(p),
						PAGE_SIZE,
						DMA_BIDIRECTIONAL);
			list_add(&p->lru, &new_free_list_items);
			page_count++;
		}
	}
	mutex_lock(&allocator->free_list_lock);
	list_splice(&new_free_list_items, &allocator->free_list_head);
	atomic_add(page_count, &allocator->free_list_size);
	mutex_unlock(&allocator->free_list_lock);
}
KBASE_EXPORT_TEST_API(kbase_mem_allocator_free);

