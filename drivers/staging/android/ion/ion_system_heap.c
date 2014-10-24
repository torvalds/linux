/*
 * drivers/gpu/ion/ion_system_heap.c
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
				     __GFP_NORETRY) & ~__GFP_WAIT;
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

static unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

struct ion_system_heap {
	struct ion_heap heap;
	struct ion_page_pool **pools;
};

struct page_info {
	struct page *page;
	unsigned int order;
	struct list_head list;
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
		page = alloc_pages(gfp_flags, order);
		if (!page)
			return NULL;
		ion_pages_sync_for_device(NULL, page, PAGE_SIZE << order,
						DMA_BIDIRECTIONAL);
	}
	if (!page)
		return NULL;

	return page;
}

static void free_buffer_page(struct ion_system_heap *heap,
			     struct ion_buffer *buffer, struct page *page,
			     unsigned int order)
{
	bool cached = ion_buffer_cached(buffer);

	if (!cached && !(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE)) {
		struct ion_page_pool *pool = heap->pools[order_to_index(order)];
		ion_page_pool_free(pool, page);
	} else {
		__free_pages(page, order);
	}
}


static struct page_info *alloc_largest_available(struct ion_system_heap *heap,
						 struct ion_buffer *buffer,
						 unsigned long size,
						 unsigned int max_order)
{
	struct page *page;
	struct page_info *info;
	int i;

	info = kmalloc(sizeof(struct page_info), GFP_KERNEL);
	if (!info)
		return NULL;

	for (i = 0; i < num_orders; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_buffer_page(heap, buffer, orders[i]);
		if (!page)
			continue;

		info->page = page;
		info->order = orders[i];
		INIT_LIST_HEAD(&info->list);
		return info;
	}
	kfree(info);

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
	int ret;
	struct list_head pages;
	struct page_info *info, *tmp_info;
	int i = 0;
	unsigned long size_remaining = PAGE_ALIGN(size);
	unsigned int max_order = orders[0];

	if (align > PAGE_SIZE)
		return -EINVAL;

	if (size / PAGE_SIZE > totalram_pages / 2)
		return -ENOMEM;

	INIT_LIST_HEAD(&pages);
	while (size_remaining > 0) {
		info = alloc_largest_available(sys_heap, buffer, size_remaining,
						max_order);
		if (!info)
			goto err;
		list_add_tail(&info->list, &pages);
		size_remaining -= (1 << info->order) * PAGE_SIZE;
		max_order = info->order;
		i++;
	}
	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		goto err;

	ret = sg_alloc_table(table, i, GFP_KERNEL);
	if (ret)
		goto err1;

	sg = table->sgl;
	list_for_each_entry_safe(info, tmp_info, &pages, list) {
		struct page *page = info->page;
		sg_set_page(sg, page, (1 << info->order) * PAGE_SIZE, 0);
		sg = sg_next(sg);
		list_del(&info->list);
		kfree(info);
	}

	buffer->priv_virt = table;
	return 0;
err1:
	kfree(table);
err:
	list_for_each_entry_safe(info, tmp_info, &pages, list) {
		free_buffer_page(sys_heap, buffer, info->page, info->order);
		kfree(info);
	}
	return -ENOMEM;
}

static void ion_system_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct ion_system_heap *sys_heap = container_of(heap,
							struct ion_system_heap,
							heap);
	struct sg_table *table = buffer->sg_table;
	bool cached = ion_buffer_cached(buffer);
	struct scatterlist *sg;
	LIST_HEAD(pages);
	int i;

	/* uncached pages come from the page pools, zero them before returning
	   for security purposes (other allocations are zerod at alloc time */
	if (!cached && !(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE))
		ion_heap_buffer_zero(buffer);

	for_each_sg(table->sgl, sg, table->nents, i)
		free_buffer_page(sys_heap, buffer, sg_page(sg),
				get_order(sg->length));
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
	return;
}

static int ion_system_heap_shrink(struct ion_heap *heap, gfp_t gfp_mask,
					int nr_to_scan)
{
	struct ion_system_heap *sys_heap;
	int nr_total = 0;
	int i;

	sys_heap = container_of(heap, struct ion_system_heap, heap);

	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];
		nr_total += ion_page_pool_shrink(pool, gfp_mask, nr_to_scan);
	}

	return nr_total;
}

#ifdef CONFIG_ROCKCHIP_IOMMU
// get device's vaddr
static int ion_system_map_iommu(struct ion_buffer *buffer,
				struct device *iommu_dev,
				struct ion_iommu_map *data,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct sg_table *table = (struct sg_table*)buffer->priv_virt;

	data->iova_addr = rockchip_iovmm_map(iommu_dev, table->sgl, 0, iova_length);
	pr_debug("%s: map %lx -> %lx\n", __func__, (unsigned long)table->sgl->dma_address, data->iova_addr);
	if (IS_ERR_VALUE(data->iova_addr)) {
		pr_err("%s: rockchip_iovmm_map() failed: %lx\n", __func__, data->iova_addr);
		ret = data->iova_addr;
		goto out;
	}

	data->mapped_size = iova_length;

out:
	return ret;
}

void ion_system_unmap_iommu(struct device *iommu_dev, struct ion_iommu_map *data)
{
	pr_debug("%s: unmap %x@%lx\n", __func__, data->mapped_size, data->iova_addr);
	rockchip_iovmm_unmap(iommu_dev, data->iova_addr);

	return;
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
#ifdef CONFIG_ROCKCHIP_IOMMU
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
			   (1 << pool->order) * PAGE_SIZE * pool->high_count);
		seq_printf(s, "%d order %u lowmem pages in pool = %lu total\n",
			   pool->low_count, pool->order,
			   (1 << pool->order) * PAGE_SIZE * pool->low_count);
	}
	return 0;
}

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *unused)
{
	struct ion_system_heap *heap;
	int i;

	heap = kzalloc(sizeof(struct ion_system_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->heap.ops = &system_heap_ops;
	heap->heap.type = ION_HEAP_TYPE_SYSTEM;
	heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;
	heap->pools = kzalloc(sizeof(struct ion_page_pool *) * num_orders,
			      GFP_KERNEL);
	if (!heap->pools)
		goto err_alloc_pools;
	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool;
		gfp_t gfp_flags = low_order_gfp_flags;

		if (orders[i] > 4)
			gfp_flags = high_order_gfp_flags;
		pool = ion_page_pool_create(gfp_flags, orders[i]);
		if (!pool)
			goto err_create_pool;
		heap->pools[i] = pool;
	}

	heap->heap.debug_show = ion_system_heap_debug_show;
	return &heap->heap;
err_create_pool:
	for (i = 0; i < num_orders; i++)
		if (heap->pools[i])
			ion_page_pool_destroy(heap->pools[i]);
	kfree(heap->pools);
err_alloc_pools:
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
	kfree(sys_heap->pools);
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

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto out;

	sg_set_page(table->sgl, page, len, 0);

	buffer->priv_virt = table;

	ion_pages_sync_for_device(NULL, page, len, DMA_BIDIRECTIONAL);

	return 0;

out:
	for (i = 0; i < len >> PAGE_SHIFT; i++)
		__free_page(page + i);
	kfree(table);
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

