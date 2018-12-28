// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/staging/android/ion/ion_system_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 */

#include <asm/page.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion.h"

#define NUM_ORDERS ARRAY_SIZE(orders)

static gfp_t high_order_gfp_flags = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN |
				     __GFP_NORETRY) & ~__GFP_RECLAIM;
static gfp_t low_order_gfp_flags  = GFP_HIGHUSER | __GFP_ZERO;
static const unsigned int orders[] = {8, 4, 0};

static int order_to_index(unsigned int order)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++)
		if (order == orders[i])
			return i;
	BUG();
	return -1;
}

static inline unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

struct ion_system_heap {
	struct ion_heap heap;
	struct ion_page_pool *pools[NUM_ORDERS];
};

static struct page *alloc_buffer_page(struct ion_system_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long order)
{
	struct ion_page_pool *pool = heap->pools[order_to_index(order)];

	return ion_page_pool_alloc(pool);
}

static void free_buffer_page(struct ion_system_heap *heap,
			     struct ion_buffer *buffer, struct page *page)
{
	struct ion_page_pool *pool;
	unsigned int order = compound_order(page);

	/* go to system */
	if (buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE) {
		__free_pages(page, order);
		return;
	}

	pool = heap->pools[order_to_index(order)];

	ion_page_pool_free(pool, page);
}

static struct page *alloc_largest_available(struct ion_system_heap *heap,
					    struct ion_buffer *buffer,
					    unsigned long size,
					    unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_buffer_page(heap, buffer, orders[i]);
		if (!page)
			continue;

		return page;
	}

	return NULL;
}

static int ion_system_heap_allocate(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    unsigned long size,
				    unsigned long flags)
{
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i = 0;
	unsigned long size_remaining = PAGE_ALIGN(size);
	unsigned int max_order = orders[0];

	if (size / PAGE_SIZE > totalram_pages() / 2)
		return -ENOMEM;

	INIT_LIST_HEAD(&pages);
	while (size_remaining > 0) {
		page = alloc_largest_available(sys_heap, buffer, size_remaining,
					       max_order);
		if (!page)
			goto free_pages;
		list_add_tail(&page->lru, &pages);
		size_remaining -= PAGE_SIZE << compound_order(page);
		max_order = compound_order(page);
		i++;
	}
	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		goto free_pages;

	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_table;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, PAGE_SIZE << compound_order(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	buffer->sg_table = table;
	return 0;

free_table:
	kfree(table);
free_pages:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		free_buffer_page(sys_heap, buffer, page);
	return -ENOMEM;
}

static void ion_system_heap_free(struct ion_buffer *buffer)
{
	struct ion_system_heap *sys_heap = container_of(buffer->heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table = buffer->sg_table;
	struct scatterlist *sg;
	int i;

	/* zero the buffer before goto page pool */
	if (!(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE))
		ion_heap_buffer_zero(buffer);

	for_each_sg(table->sgl, sg, table->nents, i)
		free_buffer_page(sys_heap, buffer, sg_page(sg));
	sg_free_table(table);
	kfree(table);
}

static int ion_system_heap_shrink(struct ion_heap *heap, gfp_t gfp_mask,
				  int nr_to_scan)
{
	struct ion_page_pool *pool;
	struct ion_system_heap *sys_heap;
	int nr_total = 0;
	int i, nr_freed;
	int only_scan = 0;

	sys_heap = container_of(heap, struct ion_system_heap, heap);

	if (!nr_to_scan)
		only_scan = 1;

	for (i = 0; i < NUM_ORDERS; i++) {
		pool = sys_heap->pools[i];

		if (only_scan) {
			nr_total += ion_page_pool_shrink(pool,
							 gfp_mask,
							 nr_to_scan);

		} else {
			nr_freed = ion_page_pool_shrink(pool,
							gfp_mask,
							nr_to_scan);
			nr_to_scan -= nr_freed;
			nr_total += nr_freed;
			if (nr_to_scan <= 0)
				break;
		}
	}
	return nr_total;
}

static struct ion_heap_ops system_heap_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
	.shrink = ion_system_heap_shrink,
};

static void ion_system_heap_destroy_pools(struct ion_page_pool **pools)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++)
		if (pools[i])
			ion_page_pool_destroy(pools[i]);
}

static int ion_system_heap_create_pools(struct ion_page_pool **pools)
{
	int i;
	gfp_t gfp_flags = low_order_gfp_flags;

	for (i = 0; i < NUM_ORDERS; i++) {
		struct ion_page_pool *pool;

		if (orders[i] > 4)
			gfp_flags = high_order_gfp_flags;

		pool = ion_page_pool_create(gfp_flags, orders[i]);
		if (!pool)
			goto err_create_pool;
		pools[i] = pool;
	}
	return 0;

err_create_pool:
	ion_system_heap_destroy_pools(pools);
	return -ENOMEM;
}

static struct ion_heap *__ion_system_heap_create(void)
{
	struct ion_system_heap *heap;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->heap.ops = &system_heap_ops;
	heap->heap.type = ION_HEAP_TYPE_SYSTEM;
	heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;

	if (ion_system_heap_create_pools(heap->pools))
		goto free_heap;

	return &heap->heap;

free_heap:
	kfree(heap);
	return ERR_PTR(-ENOMEM);
}

static int ion_system_heap_create(void)
{
	struct ion_heap *heap;

	heap = __ion_system_heap_create();
	if (IS_ERR(heap))
		return PTR_ERR(heap);
	heap->name = "ion_system_heap";

	ion_device_add_heap(heap);
	return 0;
}
device_initcall(ion_system_heap_create);

static int ion_system_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long flags)
{
	int order = get_order(len);
	struct page *page;
	struct sg_table *table;
	unsigned long i;
	int ret;

	page = alloc_pages(low_order_gfp_flags | __GFP_NOWARN, order);
	if (!page)
		return -ENOMEM;

	split_page(page, order);

	len = PAGE_ALIGN(len);
	for (i = len >> PAGE_SHIFT; i < (1 << order); i++)
		__free_page(page + i);

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto free_pages;
	}

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto free_table;

	sg_set_page(table->sgl, page, len, 0);

	buffer->sg_table = table;

	return 0;

free_table:
	kfree(table);
free_pages:
	for (i = 0; i < len >> PAGE_SHIFT; i++)
		__free_page(page + i);

	return ret;
}

static void ion_system_contig_heap_free(struct ion_buffer *buffer)
{
	struct sg_table *table = buffer->sg_table;
	struct page *page = sg_page(table->sgl);
	unsigned long pages = PAGE_ALIGN(buffer->size) >> PAGE_SHIFT;
	unsigned long i;

	for (i = 0; i < pages; i++)
		__free_page(page + i);
	sg_free_table(table);
	kfree(table);
}

static struct ion_heap_ops kmalloc_ops = {
	.allocate = ion_system_contig_heap_allocate,
	.free = ion_system_contig_heap_free,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
};

static struct ion_heap *__ion_system_contig_heap_create(void)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &kmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM_CONTIG;
	heap->name = "ion_system_contig_heap";
	return heap;
}

static int ion_system_contig_heap_create(void)
{
	struct ion_heap *heap;

	heap = __ion_system_contig_heap_create();
	if (IS_ERR(heap))
		return PTR_ERR(heap);

	ion_device_add_heap(heap);
	return 0;
}
device_initcall(ion_system_contig_heap_create);

