// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <asm/page.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/genalloc.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion.h"

#define ION_HEAP_TYPE_PROTECTED (ION_HEAP_TYPE_CUSTOM + 1)

#define NUM_ORDERS ARRAY_SIZE(orders)

static unsigned int orders[] = {8, 4, 0};

static struct reserved_mem *protected_reserved_memory;

#ifdef CONFIG_OF_RESERVED_MEM
static int __init protected_dma_setup(struct reserved_mem *rmem)
{
	protected_reserved_memory = rmem;

	pr_info("ION: created protected pool at %pa, size %ld MiB\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M);

	return 0;
}

RESERVEDMEM_OF_DECLARE(protected, "protected-dma-pool", protected_dma_setup);
#endif

static int order_to_index(unsigned int order)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++)
		if (order == orders[i])
			return i;

	return 0;
}

static inline unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

static void protected_pool_add(struct ion_page_pool *pool, struct page *page)
{
	mutex_lock(&pool->mutex);
	if (PageHighMem(page)) {
		list_add_tail(&page->lru, &pool->high_items);
		pool->high_count++;
	} else {
		list_add_tail(&page->lru, &pool->low_items);
		pool->low_count++;
	}
	mutex_unlock(&pool->mutex);
}

static struct page *protected_pool_remove(struct ion_page_pool *pool,
					  bool high)
{
	struct page *page;

	if (high) {
		page = list_first_entry(&pool->high_items, struct page, lru);
		pool->high_count--;
	} else {
		page = list_first_entry(&pool->low_items, struct page, lru);
		pool->low_count--;
	}
	list_del(&page->lru);

	return page;
}

static struct page *protected_pool_alloc(struct ion_page_pool *pool)
{
	struct page *page = NULL;

	mutex_lock(&pool->mutex);
	if (pool->high_count)
		page = protected_pool_remove(pool, true);
	else if (pool->low_count)
		page = protected_pool_remove(pool, false);
	mutex_unlock(&pool->mutex);

	return page;
}

static void protected_pool_free(struct ion_page_pool *pool,
				struct page *page)
{
	protected_pool_add(pool, page);
}

static int protected_pool_total(struct ion_page_pool *pool)
{
	return (pool->low_count + pool->high_count) << pool->order;
}

static int protected_pool_shrink(struct ion_page_pool *pool,
				 struct gen_pool *rmem,
				 int nr_to_scan)
{
	int freed = 0;

	if (nr_to_scan == 0)
		return protected_pool_total(pool);

	while (freed < nr_to_scan) {
		struct page *page;

		mutex_lock(&pool->mutex);
		if (pool->low_count) {
			page = protected_pool_remove(pool, false);
		} else if (pool->high_count) {
			page = protected_pool_remove(pool, true);
		} else {
			mutex_unlock(&pool->mutex);
			break;
		}
		mutex_unlock(&pool->mutex);
		gen_pool_free(rmem, page_to_phys(page),
			      order_to_size(pool->order));
		freed += (1 << pool->order);
	}

	return freed;
}

static struct ion_page_pool *protected_pool_create(unsigned int order)
{
	struct ion_page_pool *pool = kzalloc(sizeof(*pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	pool->high_count = 0;
	pool->low_count = 0;
	INIT_LIST_HEAD(&pool->low_items);
	INIT_LIST_HEAD(&pool->high_items);
	pool->order = order;
	mutex_init(&pool->mutex);
	plist_node_init(&pool->list, order);

	return pool;
}

static void protected_pool_destroy(struct ion_page_pool *pool)
{
	kfree(pool);
}

struct ion_protected_heap {
	struct ion_heap heap;
	struct gen_pool *rmem;
	struct ion_page_pool *pools[NUM_ORDERS];
};

struct page_info {
	struct page *page;
	struct list_head list;
	unsigned long order;
};

static void free_buffer_page(struct ion_heap *heap,
			     struct ion_buffer *buffer,
			     struct page *page,
			     unsigned long order)
{
	struct ion_page_pool *pool;
	struct ion_protected_heap *pheap;

	pheap = container_of(heap, struct ion_protected_heap, heap);
	if (buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE) {
		gen_pool_free(pheap->rmem, page_to_phys(page),
			      order_to_size(order));
		return;
	}

	pool = pheap->pools[order_to_index(order)];
	protected_pool_free(pool, page);
}

static struct page *alloc_buffer_page(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long order)
{
	struct ion_page_pool *pool;
	struct ion_protected_heap *pheap;
	struct page *page;
	unsigned long paddr;

	pheap = container_of(heap, struct ion_protected_heap, heap);
	pool = pheap->pools[order_to_index(order)];
	page = protected_pool_alloc(pool);
	if (!page) {
		paddr = gen_pool_alloc(pheap->rmem, order_to_size(order));
		if (WARN_ON(!paddr))
			return NULL;
		page = phys_to_page(paddr);
	}

	return page;
}

static struct page_info *alloc_largest_available(struct ion_heap *heap,
						 struct ion_buffer *buffer,
						 unsigned long size,
						 unsigned int max_order)
{
	struct page_info *info;
	struct page *page;
	int i;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_buffer_page(heap, buffer, orders[i]);
		if (IS_ERR(page))
			continue;

		info->page = page;
		info->order = orders[i];
		INIT_LIST_HEAD(&info->list);
		return info;
	}
	kfree(info);

	return NULL;
}

static int ion_protected_heap_allocate(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       unsigned long size,
				       unsigned long flags)
{
	struct list_head pages;
	struct list_head lists[8];
	struct page_info *info, *tmp;
	struct scatterlist *sg;
	struct sg_table *table;
	unsigned int block_index[8] = {0};
	unsigned int max_order = orders[0], maximum;
	unsigned long size_remaining = PAGE_ALIGN(size);
	int i, j;

	if (size / PAGE_SIZE > totalram_pages / 2)
		return -ENOMEM;

	INIT_LIST_HEAD(&pages);
	for (i = 0; i < 8; i++)
		INIT_LIST_HEAD(&lists[i]);

	i = 0;
	while (size_remaining > 0) {
		info = alloc_largest_available(heap, buffer, size_remaining,
					       max_order);
		if (!info)
			goto free_pages;

		size_remaining -= PAGE_SIZE << info->order;
		max_order = info->order;
		if (max_order) {
			list_add_tail(&info->list, &pages);
		} else {
			dma_addr_t phys = page_to_phys(info->page);
			unsigned int bit12_14 = (phys >> 12) & 0x7;

			list_add_tail(&info->list, &lists[bit12_14]);
			block_index[bit12_14]++;
		}

		i++;
	}

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto free_pages;

	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_table;

	maximum = block_index[0];
	for (i = 1; i < 8; i++)
		maximum = max(maximum, block_index[i]);

	sg = table->sgl;
	list_for_each_entry_safe(info, tmp, &pages, list) {
		sg_set_page(sg, info->page, PAGE_SIZE << info->order, 0);
		sg = sg_next(sg);
		list_del(&info->list);
	}

	for (i = 0; i < maximum; i++) {
		for (j = 0; j < 8; j++) {
			if (list_empty(&lists[j]))
				continue;

			info = list_first_entry(&lists[j], struct page_info,
						list);
			sg_set_page(sg, info->page, PAGE_SIZE, 0);
			sg = sg_next(sg);
			list_del(&info->list);
		}
	}
	buffer->sg_table = table;

	return 0;
free_table:
	kfree(table);
free_pages:
	list_for_each_entry_safe(info, tmp, &pages, list)
		free_buffer_page(heap, buffer, info->page, info->order);

	for (i = 0; i < 8; i++) {
		list_for_each_entry_safe(info, tmp, &lists[i], list)
			free_buffer_page(heap, buffer, info->page, info->order);
	}

	return -ENOMEM;
}

static void ion_protected_heap_free(struct ion_buffer *buffer)
{
	struct sg_table *table = buffer->sg_table;
	struct scatterlist *sg;
	int i;

	/* zero the buffer before goto page pool */
	if (!(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE))
		ion_heap_buffer_zero(buffer);

	for_each_sg(table->sgl, sg, table->nents, i)
		free_buffer_page(buffer->heap, buffer, sg_page(sg),
				 get_order(sg->length));
	sg_free_table(table);
	kfree(table);
}

static int ion_protected_heap_shrink(struct ion_heap *heap, gfp_t gfp_mask,
				     int nr_to_scan)
{
	struct ion_page_pool *pool;
	struct ion_protected_heap *pheap;
	int nr_total = 0;
	int i, nr_freed;
	int only_scan = 0;

	pheap = container_of(heap, struct ion_protected_heap, heap);
	if (!nr_to_scan)
		only_scan = 1;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pheap->pools[i];

		if (only_scan) {
			nr_total += protected_pool_shrink(pool,
							  pheap->rmem,
							  nr_to_scan);

		} else {
			nr_freed = protected_pool_shrink(pool,
							 pheap->rmem,
							 nr_to_scan);
			nr_to_scan -= nr_freed;
			nr_total += nr_freed;
			if (nr_to_scan <= 0)
				break;
		}
	}

	return nr_total;
}

static struct ion_heap_ops protected_heap_ops = {
	.allocate = ion_protected_heap_allocate,
	.free = ion_protected_heap_free,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
	.shrink = ion_protected_heap_shrink,
};

static int ion_protected_heap_debug_show(struct ion_heap *heap,
					 struct seq_file *s,
					 void *unused)
{
	struct ion_protected_heap *pheap;
	struct ion_page_pool *pool;
	int i;

	pheap = container_of(heap, struct ion_protected_heap, heap);
	for (i = 0; i < NUM_ORDERS; i++) {
		pool = pheap->pools[i];

		seq_printf(s, "%d order %u highmem pages %lu total\n",
			   pool->high_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages %lu total\n",
			   pool->low_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->low_count);
	}

	return 0;
}

static void ion_protected_heap_destroy_pools(struct ion_page_pool **pools)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++)
		if (pools[i])
			protected_pool_destroy(pools[i]);
}

static int ion_protected_heap_create_pools(struct ion_page_pool **pools)
{
	struct ion_page_pool *pool;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = protected_pool_create(orders[i]);
		if (!pool)
			goto err_create_pool;

		pools[i] = pool;
	}

	return 0;
err_create_pool:
	ion_protected_heap_destroy_pools(pools);

	return -ENOMEM;
}

static int ion_protected_heap_create_rmem(struct gen_pool **pool)
{
	struct gen_pool *mpool;
	struct reserved_mem *rmem = protected_reserved_memory;
	int ret;

	if (!rmem)
		return -ENOENT;

	mpool = gen_pool_create(PAGE_SHIFT, -1);
	if (!mpool)
		return -ENOMEM;

	ret = gen_pool_add(mpool, rmem->base, rmem->size, -1);
	if (ret) {
		gen_pool_destroy(mpool);
		return ret;
	}
	*pool = mpool;

	return 0;
}

static struct ion_heap *__ion_protected_heap_create(void)
{
	struct ion_protected_heap *heap;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	heap->heap.ops = &protected_heap_ops;
	heap->heap.type = ION_HEAP_TYPE_PROTECTED;
	heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;
	heap->heap.name = "ion_protected_heap";

	if (ion_protected_heap_create_pools(heap->pools))
		goto free_heap;

	if (ion_protected_heap_create_rmem(&heap->rmem))
		goto destroy_pool;

	heap->heap.debug_show = ion_protected_heap_debug_show;

	return &heap->heap;
destroy_pool:
	ion_protected_heap_destroy_pools(heap->pools);
free_heap:
	kfree(heap);

	return ERR_PTR(-ENOMEM);
}

int ion_protected_heap_create(void)
{
	struct ion_heap *heap;

	heap = __ion_protected_heap_create();
	if (IS_ERR(heap))
		return PTR_ERR(heap);

	ion_device_add_heap(heap);

	return 0;
}

#ifndef CONFIG_ION_MODULE
device_initcall(ion_protected_heap_create);
#endif
