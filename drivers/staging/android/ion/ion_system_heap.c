/*
 * drivers/staging/android/ion/ion_system_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
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
#include <linux/rockchip-iovmm.h>
#include "ion.h"
#include "ion_priv.h"

static gfp_t high_order_gfp_flags = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN |
				     __GFP_NORETRY) & ~__GFP_RECLAIM;
static gfp_t low_order_gfp_flags  = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN);
static const unsigned int orders[] = {8, 4, 0};
static const int num_orders = ARRAY_SIZE(orders);
static int order_to_index(unsigned int order)
{
	int i;

	for (i = 0; i < num_orders; i++)
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
	struct ion_page_pool *pools[0];
};

static struct page *alloc_buffer_page(struct ion_system_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long order)
{
	bool cached = ion_buffer_cached(buffer);
	struct ion_page_pool *pool = heap->pools[order_to_index(order)];
	struct page *page;

	if (!cached) {
		page = ion_page_pool_alloc(pool);
	} else {
		gfp_t gfp_flags = low_order_gfp_flags;

		if (order > 4)
			gfp_flags = high_order_gfp_flags;
		page = alloc_pages(gfp_flags | __GFP_COMP, order);
		if (!page)
			return NULL;
		ion_pages_sync_for_device(NULL, page, PAGE_SIZE << order,
						DMA_BIDIRECTIONAL);
	}

	return page;
}

static void free_buffer_page(struct ion_system_heap *heap,
			     struct ion_buffer *buffer, struct page *page)
{
	unsigned int order = compound_order(page);
	bool cached = ion_buffer_cached(buffer);

	if (!cached) {
		struct ion_page_pool *pool = heap->pools[order_to_index(order)];
		if (buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE)
			ion_page_pool_free_immediate(pool, page);
		else
			ion_page_pool_free(pool, page);
	} else {
		__free_pages(page, order);
	}
}


static struct page *alloc_largest_available(struct ion_system_heap *heap,
					    struct ion_buffer *buffer,
					    unsigned long size,
					    unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < num_orders; i++) {
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
				     unsigned long size, unsigned long align,
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

	struct list_head lists[8];
	unsigned int block_index[8] = {0};
	unsigned int block_1M = 0;
	unsigned int block_64K = 0;
	unsigned int maximum;
	int j;

	if (align > PAGE_SIZE)
		return -EINVAL;

	if (size / PAGE_SIZE > totalram_pages / 2)
		return -ENOMEM;

	INIT_LIST_HEAD(&pages);

	for (i = 0; i < 8; i++)
		INIT_LIST_HEAD(&lists[i]);

	i = 0;
	while (size_remaining > 0) {
		page = alloc_largest_available(sys_heap, buffer, size_remaining,
						max_order);
		if (!page)
			goto free_pages;

		size_remaining -= PAGE_SIZE << compound_order(page);
		max_order = compound_order(page);
		if (max_order) {
			if (max_order == 8)
				block_1M++;
			if (max_order == 4)
				block_64K++;
			list_add_tail(&page->lru, &pages);
		} else {
			dma_addr_t phys = page_to_phys(page);
			unsigned int bit12_14 = (phys >> 12) & 0x7;

			list_add_tail(&page->lru, &lists[bit12_14]);
			block_index[bit12_14]++;
		}

		i++;
	}

	pr_debug("%s, %d, i = %d, size = %ld\n", __func__, __LINE__, i, size);

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		goto free_pages;

	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_table;

	maximum = block_index[0];
	for (i = 1; i < 8; i++)
		maximum = max(maximum, block_index[i]);

	pr_debug("%s, %d, maximum = %d, block_1M = %d, block_64K = %d\n",
		 __func__, __LINE__, maximum, block_1M, block_64K);

	for (i = 0; i < 8; i++)
		pr_debug("block_index[%d] = %d\n", i, block_index[i]);

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, PAGE_SIZE << compound_order(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	for (i = 0; i < maximum; i++) {
		for (j = 0; j < 8; j++) {
			if (!list_empty(&lists[j])) {
				page = list_first_entry(&lists[j], struct page,
							lru);
				sg_set_page(sg, page, PAGE_SIZE, 0);
				sg = sg_next(sg);
				list_del(&page->lru);
			}
		}
	}

	buffer->priv_virt = table;
	return 0;

free_table:
	kfree(table);
free_pages:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		free_buffer_page(sys_heap, buffer, page);

	for (i = 0; i < 8; i++) {
		list_for_each_entry_safe(page, tmp_page, &lists[i], lru)
			free_buffer_page(sys_heap, buffer, page);
	}
	return -ENOMEM;
}

static void ion_system_heap_free(struct ion_buffer *buffer)
{
	struct ion_system_heap *sys_heap = container_of(buffer->heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table = buffer->sg_table;
	bool cached = ion_buffer_cached(buffer);
	struct scatterlist *sg;
	int i;

	/*
	 *  uncached pages come from the page pools, zero them before returning
	 *  for security purposes (other allocations are zerod at
	 *  alloc time
	 */
	if (!cached && !(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE))
		ion_heap_buffer_zero(buffer);

	for_each_sg(table->sgl, sg, table->nents, i)
		free_buffer_page(sys_heap, buffer, sg_page(sg));
	sg_free_table(table);
	kfree(table);
}

static struct sg_table *ion_system_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_system_heap_unmap_dma(struct ion_heap *heap,
				      struct ion_buffer *buffer)
{
}

static int ion_system_heap_shrink(struct ion_heap *heap, gfp_t gfp_mask,
					int nr_to_scan)
{
	struct ion_system_heap *sys_heap;
	int nr_total = 0;
	int i, nr_freed;
	int only_scan = 0;

	sys_heap = container_of(heap, struct ion_system_heap, heap);

	if (!nr_to_scan)
		only_scan = 1;

	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];

		nr_freed = ion_page_pool_shrink(pool, gfp_mask, nr_to_scan);
		nr_total += nr_freed;

		if (!only_scan) {
			nr_to_scan -= nr_freed;
			/* shrink completed */
			if (nr_to_scan <= 0)
				break;
		}
	}

	return nr_total;
}

#ifdef CONFIG_RK_IOMMU
static int ion_system_map_iommu(struct ion_buffer *buffer,
				struct device *iommu_dev,
				struct ion_iommu_map *data,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct sg_table *table = (struct sg_table *)buffer->priv_virt;

	data->iova_addr = rockchip_iovmm_map(iommu_dev,
					     table->sgl, 0,
					     iova_length);
	pr_debug("%s: map %lx -> %lx\n", __func__,
		 (unsigned long)table->sgl->dma_address,
		 data->iova_addr);
	if (IS_ERR_VALUE(data->iova_addr)) {
		pr_err("%s: rockchip_iovmm_map() failed: 0x%lx\n",
		       __func__, data->iova_addr);
		ret = data->iova_addr;
		goto out;
	}

	data->mapped_size = iova_length;
out:
	return ret;
}

void ion_system_unmap_iommu(struct device *iommu_dev,
			    struct ion_iommu_map *data)
{
	pr_debug("%s: unmap 0x%x@0x%lx\n", __func__,
		 data->mapped_size, data->iova_addr);
	rockchip_iovmm_unmap(iommu_dev, data->iova_addr);
}
#endif

static struct ion_heap_ops system_heap_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_dma = ion_system_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
	.shrink = ion_system_heap_shrink,
#ifdef CONFIG_RK_IOMMU
	.map_iommu = ion_system_map_iommu,
	.unmap_iommu = ion_system_unmap_iommu,
#endif
};

static int ion_system_heap_debug_show(struct ion_heap *heap, struct seq_file *s,
				      void *unused)
{

	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	int i;

	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];

		seq_printf(s, "%d order %u highmem pages in pool = %lu total\n",
			   pool->high_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages in pool = %lu total\n",
			   pool->low_count, pool->order,
			   (PAGE_SIZE << pool->order) * pool->low_count);
	}
	return 0;
}

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *unused)
{
	struct ion_system_heap *heap;
	int i;

	heap = kzalloc(sizeof(struct ion_system_heap) +
			sizeof(struct ion_page_pool *) * num_orders,
			GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->heap.ops = &system_heap_ops;
	heap->heap.type = ION_HEAP_TYPE_SYSTEM;
	heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;

	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool;
		gfp_t gfp_flags = low_order_gfp_flags;

		if (orders[i] > 4)
			gfp_flags = high_order_gfp_flags;
		pool = ion_page_pool_create(gfp_flags, orders[i]);
		if (!pool)
			goto destroy_pools;
		heap->pools[i] = pool;
	}

	heap->heap.debug_show = ion_system_heap_debug_show;
	return &heap->heap;

destroy_pools:
	while (i--)
		ion_page_pool_destroy(heap->pools[i]);
	kfree(heap);
	return ERR_PTR(-ENOMEM);
}

void ion_system_heap_destroy(struct ion_heap *heap)
{
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	int i;

	for (i = 0; i < num_orders; i++)
		ion_page_pool_destroy(sys_heap->pools[i]);
	kfree(sys_heap);
}

static int ion_system_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	int order = get_order(len);
	struct page *page;
	struct sg_table *table;
	unsigned long i;
	int ret;

	if (align > (PAGE_SIZE << order))
		return -EINVAL;

	page = alloc_pages(low_order_gfp_flags, order);
	if (!page)
		return -ENOMEM;

	split_page(page, order);

	len = PAGE_ALIGN(len);
	for (i = len >> PAGE_SHIFT; i < (1 << order); i++)
		__free_page(page + i);

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto free_pages;
	}

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto free_table;

	sg_set_page(table->sgl, page, len, 0);

	buffer->priv_virt = table;

	ion_pages_sync_for_device(NULL, page, len, DMA_BIDIRECTIONAL);

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
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	unsigned long pages = PAGE_ALIGN(buffer->size) >> PAGE_SHIFT;
	unsigned long i;

	for (i = 0; i < pages; i++)
		__free_page(page + i);
	sg_free_table(table);
	kfree(table);
}

static int ion_system_contig_heap_phys(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       ion_phys_addr_t *addr, size_t *len)
{
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	*addr = page_to_phys(page);
	*len = buffer->size;
	return 0;
}

static struct sg_table *ion_system_contig_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_system_contig_heap_unmap_dma(struct ion_heap *heap,
					     struct ion_buffer *buffer)
{
}

static struct ion_heap_ops kmalloc_ops = {
	.allocate = ion_system_contig_heap_allocate,
	.free = ion_system_contig_heap_free,
	.phys = ion_system_contig_heap_phys,
	.map_dma = ion_system_contig_heap_map_dma,
	.unmap_dma = ion_system_contig_heap_unmap_dma,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
};

struct ion_heap *ion_system_contig_heap_create(struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &kmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM_CONTIG;
	return heap;
}

void ion_system_contig_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}
