/*
 * drivers/staging/android/ion/ion_chunk_heap.c
 *
 * Copyright (C) 2012 Google, Inc.
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
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion.h"
#include "ion_priv.h"

struct ion_chunk_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	ion_phys_addr_t base;
	unsigned long chunk_size;
	unsigned long size;
	unsigned long allocated;
};

static int ion_chunk_heap_allocate(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long size, unsigned long align,
				      unsigned long flags)
{
	struct ion_chunk_heap *chunk_heap =
		container_of(heap, struct ion_chunk_heap, heap);
	struct sg_table *table;
	struct scatterlist *sg;
	int ret, i;
	unsigned long num_chunks;
	unsigned long allocated_size;

	if (align > chunk_heap->chunk_size)
		return -EINVAL;

	allocated_size = ALIGN(size, chunk_heap->chunk_size);
	num_chunks = allocated_size / chunk_heap->chunk_size;

	if (allocated_size > chunk_heap->size - chunk_heap->allocated)
		return -ENOMEM;

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	ret = sg_alloc_table(table, num_chunks, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ret;
	}

	sg = table->sgl;
	for (i = 0; i < num_chunks; i++) {
		unsigned long paddr = gen_pool_alloc(chunk_heap->pool,
						     chunk_heap->chunk_size);
		if (!paddr)
			goto err;
		sg_set_page(sg, pfn_to_page(PFN_DOWN(paddr)),
				chunk_heap->chunk_size, 0);
		sg = sg_next(sg);
	}

	buffer->priv_virt = table;
	chunk_heap->allocated += allocated_size;
	return 0;
err:
	sg = table->sgl;
	for (i -= 1; i >= 0; i--) {
		gen_pool_free(chunk_heap->pool, page_to_phys(sg_page(sg)),
			      sg->length);
		sg = sg_next(sg);
	}
	sg_free_table(table);
	kfree(table);
	return -ENOMEM;
}

static void ion_chunk_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct ion_chunk_heap *chunk_heap =
		container_of(heap, struct ion_chunk_heap, heap);
	struct sg_table *table = buffer->priv_virt;
	struct scatterlist *sg;
	int i;
	unsigned long allocated_size;

	allocated_size = ALIGN(buffer->size, chunk_heap->chunk_size);

	ion_heap_buffer_zero(buffer);

	if (ion_buffer_cached(buffer))
		dma_sync_sg_for_device(NULL, table->sgl, table->nents,
							DMA_BIDIRECTIONAL);

	for_each_sg(table->sgl, sg, table->nents, i) {
		gen_pool_free(chunk_heap->pool, page_to_phys(sg_page(sg)),
			      sg->length);
	}
	chunk_heap->allocated -= allocated_size;
	sg_free_table(table);
	kfree(table);
}

static struct sg_table *ion_chunk_heap_map_dma(struct ion_heap *heap,
					       struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_chunk_heap_unmap_dma(struct ion_heap *heap,
				     struct ion_buffer *buffer)
{
	return;
}

static struct ion_heap_ops chunk_heap_ops = {
	.allocate = ion_chunk_heap_allocate,
	.free = ion_chunk_heap_free,
	.map_dma = ion_chunk_heap_map_dma,
	.unmap_dma = ion_chunk_heap_unmap_dma,
	.map_user = ion_heap_map_user,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
};

struct ion_heap *ion_chunk_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_chunk_heap *chunk_heap;
	int ret;
	struct page *page;
	size_t size;

	page = pfn_to_page(PFN_DOWN(heap_data->base));
	size = heap_data->size;

	ion_pages_sync_for_device(NULL, page, size, DMA_BIDIRECTIONAL);

	ret = ion_heap_pages_zero(page, size, pgprot_writecombine(PAGE_KERNEL));
	if (ret)
		return ERR_PTR(ret);

	chunk_heap = kzalloc(sizeof(struct ion_chunk_heap), GFP_KERNEL);
	if (!chunk_heap)
		return ERR_PTR(-ENOMEM);

	chunk_heap->chunk_size = (unsigned long)heap_data->priv;
	chunk_heap->pool = gen_pool_create(get_order(chunk_heap->chunk_size) +
					   PAGE_SHIFT, -1);
	if (!chunk_heap->pool) {
		ret = -ENOMEM;
		goto error_gen_pool_create;
	}
	chunk_heap->base = heap_data->base;
	chunk_heap->size = heap_data->size;
	chunk_heap->allocated = 0;

	gen_pool_add(chunk_heap->pool, chunk_heap->base, heap_data->size, -1);
	chunk_heap->heap.ops = &chunk_heap_ops;
	chunk_heap->heap.type = ION_HEAP_TYPE_CHUNK;
	chunk_heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;
	pr_info("%s: base %lu size %zu align %ld\n", __func__, chunk_heap->base,
		heap_data->size, heap_data->align);

	return &chunk_heap->heap;

error_gen_pool_create:
	kfree(chunk_heap);
	return ERR_PTR(ret);
}

void ion_chunk_heap_destroy(struct ion_heap *heap)
{
	struct ion_chunk_heap *chunk_heap =
	     container_of(heap, struct  ion_chunk_heap, heap);

	gen_pool_destroy(chunk_heap->pool);
	kfree(chunk_heap);
	chunk_heap = NULL;
}
