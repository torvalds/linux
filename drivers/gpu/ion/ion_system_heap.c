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

#include <linux/err.h>
#include <linux/ion.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion_priv.h"

static int ion_system_heap_allocate(struct ion_heap *heap,
				     struct ion_buffer *buffer,
				     unsigned long size, unsigned long align,
				     unsigned long flags)
{
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;
	int npages = PAGE_ALIGN(size) / PAGE_SIZE;

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	i = sg_alloc_table(table, npages, GFP_KERNEL);
	if (i)
		goto err0;
	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page;
		page = alloc_page(GFP_HIGHUSER | __GFP_ZERO);
		if (!page)
			goto err1;
		sg_set_page(sg, page, PAGE_SIZE, 0);
	}
	buffer->priv_virt = table;
	return 0;
err1:
	for_each_sg(table->sgl, sg, i, j)
		__free_page(sg_page(sg));
	sg_free_table(table);
err0:
	kfree(table);
	return -ENOMEM;
}

void ion_system_heap_free(struct ion_buffer *buffer)
{
	int i;
	struct scatterlist *sg;
	struct sg_table *table = buffer->priv_virt;

	for_each_sg(table->sgl, sg, table->nents, i)
		__free_page(sg_page(sg));
	if (buffer->sg_table)
		sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
}

struct sg_table *ion_system_heap_map_dma(struct ion_heap *heap,
					 struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

void ion_system_heap_unmap_dma(struct ion_heap *heap,
			       struct ion_buffer *buffer)
{
	return;
}

void *ion_system_heap_map_kernel(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	struct scatterlist *sg;
	int i;
	void *vaddr;
	pgprot_t pgprot;
	struct sg_table *table = buffer->priv_virt;
	struct page **pages = kmalloc(sizeof(struct page *) * table->nents,
				      GFP_KERNEL);

	for_each_sg(table->sgl, sg, table->nents, i)
		pages[i] = sg_page(sg);

	if (buffer->flags & ION_FLAG_CACHED)
		pgprot = PAGE_KERNEL;
	else
		pgprot = pgprot_writecombine(PAGE_KERNEL);

	vaddr = vmap(pages, table->nents, VM_MAP, pgprot);
	kfree(pages);

	return vaddr;
}

void ion_system_heap_unmap_kernel(struct ion_heap *heap,
				  struct ion_buffer *buffer)
{
	vunmap(buffer->vaddr);
}

int ion_system_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
			     struct vm_area_struct *vma)
{
	struct sg_table *table = buffer->priv_virt;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff;
	struct scatterlist *sg;
	int i;

	for_each_sg(table->sgl, sg, table->nents, i) {
		if (offset) {
			offset--;
			continue;
		}
		vm_insert_page(vma, addr, sg_page(sg));
		addr += PAGE_SIZE;
	}
	return 0;
}

static struct ion_heap_ops vmalloc_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_dma = ion_system_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_system_heap_map_kernel,
	.unmap_kernel = ion_system_heap_unmap_kernel,
	.map_user = ion_system_heap_map_user,
};

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *unused)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &vmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM;
	return heap;
}

void ion_system_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

static int ion_system_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	buffer->priv_virt = kzalloc(len, GFP_KERNEL);
	if (!buffer->priv_virt)
		return -ENOMEM;
	return 0;
}

void ion_system_contig_heap_free(struct ion_buffer *buffer)
{
	kfree(buffer->priv_virt);
}

static int ion_system_contig_heap_phys(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       ion_phys_addr_t *addr, size_t *len)
{
	*addr = virt_to_phys(buffer->priv_virt);
	*len = buffer->size;
	return 0;
}

struct sg_table *ion_system_contig_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	struct sg_table *table;
	int ret;

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ERR_PTR(ret);
	}
	sg_set_page(table->sgl, virt_to_page(buffer->priv_virt), buffer->size,
		    0);
	return table;
}

void ion_system_contig_heap_unmap_dma(struct ion_heap *heap,
				      struct ion_buffer *buffer)
{
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
}

int ion_system_contig_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	unsigned long pfn = __phys_to_pfn(virt_to_phys(buffer->priv_virt));
	return remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);

}

static struct ion_heap_ops kmalloc_ops = {
	.allocate = ion_system_contig_heap_allocate,
	.free = ion_system_contig_heap_free,
	.phys = ion_system_contig_heap_phys,
	.map_dma = ion_system_contig_heap_map_dma,
	.unmap_dma = ion_system_contig_heap_unmap_dma,
	.map_kernel = ion_system_heap_map_kernel,
	.unmap_kernel = ion_system_heap_unmap_kernel,
	.map_user = ion_system_contig_heap_map_user,
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

