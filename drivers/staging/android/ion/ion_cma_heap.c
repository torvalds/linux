/*
 * drivers/gpu/ion/ion_cma_heap.c
 *
 * Copyright (C) Linaro 2012
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
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

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/rockchip_ion.h>
#include <linux/rockchip-iovmm.h>

#include "ion.h"
#include "ion_priv.h"

#define ION_CMA_ALLOCATE_FAILED -1

struct ion_cma_heap {
	struct ion_heap heap;
	struct device *dev;
};

#define to_cma_heap(x) container_of(x, struct ion_cma_heap, heap)

struct ion_cma_buffer_info {
	void *cpu_addr;
	dma_addr_t handle;
	struct sg_table *table;
};

/*
 * Create scatter-list for the already allocated DMA buffer.
 * This function could be replaced by dma_common_get_sgtable
 * as soon as it will avalaible.
 */
static int ion_cma_get_sgtable(struct device *dev, struct sg_table *sgt,
			       dma_addr_t handle, size_t size)
{
	struct page *page = phys_to_page(handle);
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return 0;
}

/* ION CMA heap operations functions */
static int ion_cma_allocate(struct ion_heap *heap, struct ion_buffer *buffer,
			    unsigned long len, unsigned long align,
			    unsigned long flags)
{
	struct ion_cma_heap *cma_heap = to_cma_heap(heap);
	struct device *dev = cma_heap->dev;
	struct ion_cma_buffer_info *info;
	DEFINE_DMA_ATTRS(attrs);

#ifdef CONFIG_ION_CMA_HIGHMEM
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);
#endif
	dev_dbg(dev, "Request buffer allocation len %ld\n", len);

	if (buffer->flags & ION_FLAG_CACHED)
		return -EINVAL;

	if (align > PAGE_SIZE)
		return -EINVAL;

	info = kzalloc(sizeof(struct ion_cma_buffer_info), GFP_KERNEL);
	if (!info) {
		dev_err(dev, "Can't allocate buffer info\n");
		return ION_CMA_ALLOCATE_FAILED;
	}

	info->cpu_addr = dma_alloc_attrs(dev, len, &(info->handle),
						GFP_HIGHUSER | __GFP_ZERO, &attrs);

	if (!info->cpu_addr) {
		dev_err(dev, "Fail to allocate(%lx) buffer\n", len);
		goto err;
	}

	info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!info->table) {
		dev_err(dev, "Fail to allocate sg table\n");
		goto free_mem;
	}

	if (ion_cma_get_sgtable(dev, info->table, info->handle, len))
		goto free_table;
	/* keep this for memory release */
	buffer->priv_virt = info;
	dev_dbg(dev, "Allocate buffer %p\n", buffer);
	return 0;

free_table:
	kfree(info->table);
free_mem:
	dma_free_attrs(dev, len, info->cpu_addr, info->handle, &attrs);
err:
	kfree(info);
	return ION_CMA_ALLOCATE_FAILED;
}

static void ion_cma_free(struct ion_buffer *buffer)
{
	struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
	struct device *dev = cma_heap->dev;
	struct ion_cma_buffer_info *info = buffer->priv_virt;
	DEFINE_DMA_ATTRS(attrs);

#ifdef CONFIG_ION_CMA_HIGHMEM
	dma_set_attr(DMA_ATTR_NO_KERNEL_MAPPING, &attrs);
#endif
	dev_dbg(dev, "Release buffer %p\n", buffer);
	/* release memory */
	dma_free_attrs(dev, buffer->size, info->cpu_addr, info->handle, &attrs);
	/* release sg table */
	sg_free_table(info->table);
	kfree(info->table);
	kfree(info);
}

/* return physical address in addr */
static int ion_cma_phys(struct ion_heap *heap, struct ion_buffer *buffer,
			ion_phys_addr_t *addr, size_t *len)
{
	struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
	struct device *dev = cma_heap->dev;
	struct ion_cma_buffer_info *info = buffer->priv_virt;

	dev_dbg(dev, "Return buffer %p physical address 0x%pa\n", buffer,
		&info->handle);

	*addr = info->handle;
	*len = buffer->size;

	return 0;
}

static struct sg_table *ion_cma_heap_map_dma(struct ion_heap *heap,
					     struct ion_buffer *buffer)
{
	struct ion_cma_buffer_info *info = buffer->priv_virt;

	return info->table;
}

static void ion_cma_heap_unmap_dma(struct ion_heap *heap,
				   struct ion_buffer *buffer)
{
	return;
}

static int ion_cma_mmap(struct ion_heap *mapper, struct ion_buffer *buffer,
			struct vm_area_struct *vma)
{
	struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
	struct device *dev = cma_heap->dev;
	struct ion_cma_buffer_info *info = buffer->priv_virt;

	return dma_mmap_coherent(dev, vma, info->cpu_addr, info->handle,
				 buffer->size);
}

#ifndef CONFIG_ION_CMA_HIGHMEM
static void *ion_cma_map_kernel(struct ion_heap *heap,
				struct ion_buffer *buffer)
{
	struct ion_cma_buffer_info *info = buffer->priv_virt;
	/* kernel memory mapping has been done at allocation time */
	return info->cpu_addr;
}

static void ion_cma_unmap_kernel(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
}
#endif

#ifdef CONFIG_ROCKCHIP_IOMMU
// get device's vaddr
static int ion_cma_map_iommu(struct ion_buffer *buffer,
				struct device *iommu_dev,
				struct ion_iommu_map *data,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct ion_cma_buffer_info *info = buffer->priv_virt;

	data->iova_addr = rockchip_iovmm_map(iommu_dev, info->table->sgl, 0, iova_length);
	pr_debug("%s: map %x -> %lx\n", __func__, info->table->sgl->dma_address,
		data->iova_addr);
	if (IS_ERR_VALUE(data->iova_addr)) {
		pr_err("%s: rockchip_iovmm_map() failed: %lx\n", __func__, data->iova_addr);
		ret = data->iova_addr;
		goto out;
	}

	data->mapped_size = iova_length;

out:
	return ret;
}

void ion_cma_unmap_iommu(struct device *iommu_dev, struct ion_iommu_map *data)
{
	pr_debug("%s: unmap %x@%lx\n", __func__, data->mapped_size, data->iova_addr);
	rockchip_iovmm_unmap(iommu_dev, data->iova_addr);

	return;
}
#endif

static struct ion_heap_ops ion_cma_ops = {
	.allocate = ion_cma_allocate,
	.free = ion_cma_free,
	.map_dma = ion_cma_heap_map_dma,
	.unmap_dma = ion_cma_heap_unmap_dma,
	.phys = ion_cma_phys,
	.map_user = ion_cma_mmap,
#ifdef CONFIG_ION_CMA_HIGHMEM
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
#else
	.map_kernel = ion_cma_map_kernel,
	.unmap_kernel = ion_cma_unmap_kernel,
#endif
#ifdef CONFIG_ROCKCHIP_IOMMU
	.map_iommu = ion_cma_map_iommu,
	.unmap_iommu = ion_cma_unmap_iommu,
#endif
};

struct ion_heap *ion_cma_heap_create(struct ion_platform_heap *data)
{
	struct ion_cma_heap *cma_heap;

	cma_heap = kzalloc(sizeof(struct ion_cma_heap), GFP_KERNEL);

	if (!cma_heap)
		return ERR_PTR(-ENOMEM);

	cma_heap->heap.ops = &ion_cma_ops;
	/* get device from private heaps data, later it will be
	 * used to make the link with reserved CMA memory */
	cma_heap->dev = data->priv;
	cma_heap->heap.type = ION_HEAP_TYPE_DMA;
	return &cma_heap->heap;
}

void ion_cma_heap_destroy(struct ion_heap *heap)
{
	struct ion_cma_heap *cma_heap = to_cma_heap(heap);

	kfree(cma_heap);
}
