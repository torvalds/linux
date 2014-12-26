/*
 * drivers/gpu/ion/ion_drm_heap.c
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
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/rockchip-iovmm.h>
#include "ion.h"
#include "ion_priv.h"

#define ION_DRM_ALLOCATE_FAILED -1

struct ion_drm_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	ion_phys_addr_t base;
};

ion_phys_addr_t ion_drm_allocate(struct ion_heap *heap,
				      unsigned long size,
				      unsigned long align)
{
	struct ion_drm_heap *drm_heap =
		container_of(heap, struct ion_drm_heap, heap);
	unsigned long offset = gen_pool_alloc(drm_heap->pool, size);

	if (!offset)
		return ION_DRM_ALLOCATE_FAILED;

	return offset;
}

void ion_drm_free(struct ion_heap *heap, ion_phys_addr_t addr,
		       unsigned long size)
{
	struct ion_drm_heap *drm_heap =
		container_of(heap, struct ion_drm_heap, heap);

	if (addr == ION_DRM_ALLOCATE_FAILED)
		return;
	gen_pool_free(drm_heap->pool, addr, size);
}

static int ion_drm_heap_phys(struct ion_heap *heap,
				  struct ion_buffer *buffer,
				  ion_phys_addr_t *addr, size_t *len)
{
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	*addr = paddr;
	*len = buffer->size;
	return 0;
}

static int ion_drm_heap_allocate(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long size, unsigned long align,
				      unsigned long flags)
{
	struct sg_table *table;
	ion_phys_addr_t paddr;
	int ret;

	if (align > PAGE_SIZE)
		return -EINVAL;

	if (ion_buffer_cached(buffer)) {
		pr_err("%s: cannot allocate cached memory from secure heap %s\n",
			__func__, heap->name);
		return -ENOMEM;
	}

	table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_free;

	paddr = ion_drm_allocate(heap, size, align);
	if (paddr == ION_DRM_ALLOCATE_FAILED) {
		ret = -ENOMEM;
		goto err_free_table;
	}

	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), size, 0);
	buffer->priv_virt = table;

	return 0;

err_free_table:
	sg_free_table(table);
err_free:
	kfree(table);
	return ret;
}

static void ion_drm_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct sg_table *table = buffer->priv_virt;
	struct page *page = sg_page(table->sgl);
	ion_phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	ion_heap_buffer_zero(buffer);
	ion_drm_free(heap, paddr, buffer->size);
	sg_free_table(table);
	kfree(table);
}

static struct sg_table *ion_drm_heap_map_dma(struct ion_heap *heap,
						  struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_drm_heap_unmap_dma(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
	return;
}

static int ion_drm_heap_mmap(struct ion_heap *mapper,
                        struct ion_buffer *buffer,
                        struct vm_area_struct *vma)
{
        pr_info("%s: mmaping from secure heap %s disallowed\n",
                __func__, mapper->name);
        return -EINVAL;
}

static void *ion_drm_heap_map_kernel(struct ion_heap *heap,
                                struct ion_buffer *buffer)
{
        pr_info("%s: kernel mapping from secure heap %s disallowed\n",
                __func__, heap->name);
        return NULL;
}

static void ion_drm_heap_unmap_kernel(struct ion_heap *heap,
                                 struct ion_buffer *buffer)
{
        return;
}

#ifdef CONFIG_ROCKCHIP_IOMMU
static int ion_drm_heap_map_iommu(struct ion_buffer *buffer,
				struct device *iommu_dev,
				struct ion_iommu_map *data,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct sg_table *table = (struct sg_table*)buffer->priv_virt;

	data->iova_addr = rockchip_iovmm_map(iommu_dev, table->sgl, 0, iova_length);
	pr_debug("%s: map %lx -> %lx\n", __func__, (unsigned long)table->sgl->dma_address,
		data->iova_addr);
	if (IS_ERR_VALUE(data->iova_addr)) {
		pr_err("%s: rockchip_iovmm_map() failed: %lx\n", __func__,
			data->iova_addr);
		ret = data->iova_addr;
		goto out;
	}

	data->mapped_size = iova_length;

out:
	return ret;
}

void ion_drm_heap_unmap_iommu(struct device *iommu_dev,
			struct ion_iommu_map *data)
{
	pr_debug("%s: unmap %x@%lx\n", __func__, data->mapped_size,
		data->iova_addr);
	rockchip_iovmm_unmap(iommu_dev, data->iova_addr);

	return;
}
#endif

static struct ion_heap_ops drm_heap_ops = {
	.allocate = ion_drm_heap_allocate,
	.free = ion_drm_heap_free,
	.phys = ion_drm_heap_phys,
	.map_dma = ion_drm_heap_map_dma,
	.unmap_dma = ion_drm_heap_unmap_dma,
	.map_user = ion_drm_heap_mmap,
	.map_kernel = ion_drm_heap_map_kernel,
	.unmap_kernel = ion_drm_heap_unmap_kernel,
#ifdef CONFIG_ROCKCHIP_IOMMU
	.map_iommu = ion_drm_heap_map_iommu,
	.unmap_iommu = ion_drm_heap_unmap_iommu,
#endif
};

struct ion_heap *ion_drm_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_drm_heap *drm_heap;
	int ret;

	struct page *page;
	size_t size;

	page = pfn_to_page(PFN_DOWN(heap_data->base));
	size = heap_data->size;

	printk("%s: %zx@%lx\n", __func__, size, heap_data->base);

	ion_pages_sync_for_device(NULL, page, size, DMA_BIDIRECTIONAL);

	ret = ion_heap_pages_zero(page, size, pgprot_writecombine(PAGE_KERNEL));
	if (ret)
		return ERR_PTR(ret);

	drm_heap = kzalloc(sizeof(struct ion_drm_heap), GFP_KERNEL);
	if (!drm_heap)
		return ERR_PTR(-ENOMEM);

	drm_heap->pool = gen_pool_create(8, -1); // 256KB align
	if (!drm_heap->pool) {
		kfree(drm_heap);
		return ERR_PTR(-ENOMEM);
	}
	drm_heap->base = heap_data->base;
	gen_pool_add(drm_heap->pool, drm_heap->base, heap_data->size, -1);
	drm_heap->heap.ops = &drm_heap_ops;
	drm_heap->heap.type = ION_HEAP_TYPE_DRM;
//	drm_heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;

	return &drm_heap->heap;
}

void ion_drm_heap_destroy(struct ion_heap *heap)
{
	struct ion_drm_heap *drm_heap =
	     container_of(heap, struct  ion_drm_heap, heap);

	gen_pool_destroy(drm_heap->pool);
	kfree(drm_heap);
	drm_heap = NULL;
}
