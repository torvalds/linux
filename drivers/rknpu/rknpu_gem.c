// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co.Ltd
 * Author: Felix Zeng <felix.zeng@rock-chips.com>
 */

#include <drm/drm_device.h>
#include <drm/drm_vma_manager.h>
#include <drm/drm_prime.h>
#include <drm/drm_file.h>
#include <drm/drm_drv.h>

#include <linux/shmem_fs.h>
#include <linux/dma-buf.h>
#include <linux/iommu.h>
#include <linux/dma-iommu.h>
#include <linux/pfn_t.h>
#include <linux/version.h>
#include <asm/cacheflush.h>

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
#include <linux/dma-map-ops.h>
#endif

#include "rknpu_drv.h"
#include "rknpu_ioctl.h"
#include "rknpu_gem.h"

#define RKNPU_GEM_ALLOC_FROM_PAGES 1

#if RKNPU_GEM_ALLOC_FROM_PAGES
static int rknpu_gem_get_pages(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct scatterlist *s = NULL;
	dma_addr_t dma_addr = 0;
	dma_addr_t phys = 0;
	int ret = -EINVAL, i = 0;

	rknpu_obj->pages = drm_gem_get_pages(&rknpu_obj->base);
	if (IS_ERR(rknpu_obj->pages)) {
		ret = PTR_ERR(rknpu_obj->pages);
		LOG_ERROR("failed to get pages: %d\n", ret);
		return ret;
	}

	rknpu_obj->num_pages = rknpu_obj->size >> PAGE_SHIFT;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rknpu_obj->sgt = drm_prime_pages_to_sg(drm, rknpu_obj->pages,
					       rknpu_obj->num_pages);
#else
	rknpu_obj->sgt =
		drm_prime_pages_to_sg(rknpu_obj->pages, rknpu_obj->num_pages);
#endif
	if (IS_ERR(rknpu_obj->sgt)) {
		ret = PTR_ERR(rknpu_obj->sgt);
		LOG_ERROR("failed to allocate sgt: %d\n", ret);
		goto put_pages;
	}

	ret = dma_map_sg(drm->dev, rknpu_obj->sgt->sgl, rknpu_obj->sgt->nents,
			 DMA_BIDIRECTIONAL);
	if (ret == 0) {
		ret = -EFAULT;
		LOG_DEV_ERROR(drm->dev, "%s: dma map %zu fail\n", __func__,
			      rknpu_obj->size);
		goto free_sgt;
	}

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING) {
		rknpu_obj->cookie = vmap(rknpu_obj->pages, rknpu_obj->num_pages,
					 VM_MAP, PAGE_KERNEL);
		if (!rknpu_obj->cookie) {
			ret = -ENOMEM;
			LOG_ERROR("failed to vmap: %d\n", ret);
			goto unmap_sg;
		}
		rknpu_obj->kv_addr = rknpu_obj->cookie;
	}

	dma_addr = sg_dma_address(rknpu_obj->sgt->sgl);
	rknpu_obj->dma_addr = dma_addr;

	for_each_sg(rknpu_obj->sgt->sgl, s, rknpu_obj->sgt->nents, i) {
		dma_addr += s->length;
		phys = sg_phys(s);
		LOG_DEBUG(
			"gem pages alloc sgt[%d], dma_address: %pad, length: %#x, phys: %pad, virt: %p\n",
			i, &dma_addr, s->length, &phys, sg_virt(s));
	}

	return 0;

unmap_sg:
	dma_unmap_sg(drm->dev, rknpu_obj->sgt->sgl, rknpu_obj->sgt->nents,
		     DMA_BIDIRECTIONAL);

free_sgt:
	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);

put_pages:
	drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, false, false);

	return ret;
}

static void rknpu_gem_put_pages(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING) {
		vunmap(rknpu_obj->kv_addr);
		rknpu_obj->kv_addr = NULL;
	}

	dma_unmap_sg(drm->dev, rknpu_obj->sgt->sgl, rknpu_obj->sgt->nents,
		     DMA_BIDIRECTIONAL);

	drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, true, true);

	if (rknpu_obj->sgt != NULL) {
		sg_free_table(rknpu_obj->sgt);
		kfree(rknpu_obj->sgt);
	}
}
#endif

static int rknpu_gem_alloc_buf(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct rknpu_device *rknpu_dev = drm->dev_private;
	unsigned int nr_pages = 0;
	struct sg_table *sgt = NULL;
	struct scatterlist *s = NULL;
	gfp_t gfp_mask = GFP_KERNEL;
	int ret = -EINVAL, i = 0;

	if (rknpu_obj->dma_addr) {
		LOG_DEBUG("buffer already allocated.\n");
		return 0;
	}

	rknpu_obj->dma_attrs = 0;

	/*
	 * if RKNPU_MEM_CONTIGUOUS, fully physically contiguous memory
	 * region will be allocated else physically contiguous
	 * as possible.
	 */
	if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS))
		rknpu_obj->dma_attrs |= DMA_ATTR_FORCE_CONTIGUOUS;

	// cacheable mapping or writecombine mapping
	if (rknpu_obj->flags & RKNPU_MEM_CACHEABLE) {
#ifdef DMA_ATTR_NON_CONSISTENT
		rknpu_obj->dma_attrs |= DMA_ATTR_NON_CONSISTENT;
#endif
#ifdef DMA_ATTR_SYS_CACHE_ONLY
		rknpu_obj->dma_attrs |= DMA_ATTR_SYS_CACHE_ONLY;
#endif
	} else if (rknpu_obj->flags & RKNPU_MEM_WRITE_COMBINE) {
		rknpu_obj->dma_attrs |= DMA_ATTR_WRITE_COMBINE;
	}

	if (!(rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING))
		rknpu_obj->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;

#ifdef DMA_ATTR_SKIP_ZEROING
	if (!(rknpu_obj->flags & RKNPU_MEM_ZEROING))
		rknpu_obj->dma_attrs |= DMA_ATTR_SKIP_ZEROING;
#endif

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		return rknpu_gem_get_pages(rknpu_obj);
	}
#endif

	if (rknpu_obj->flags & RKNPU_MEM_ZEROING)
		gfp_mask |= __GFP_ZERO;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_DMA32)) {
		gfp_mask &= ~__GFP_HIGHMEM;
		gfp_mask |= __GFP_DMA32;
	}

	nr_pages = rknpu_obj->size >> PAGE_SHIFT;

	rknpu_obj->pages = rknpu_gem_alloc_page(nr_pages);
	if (!rknpu_obj->pages) {
		LOG_ERROR("failed to allocate pages.\n");
		return -ENOMEM;
	}

	rknpu_obj->cookie =
		dma_alloc_attrs(drm->dev, rknpu_obj->size, &rknpu_obj->dma_addr,
				gfp_mask, rknpu_obj->dma_attrs);
	if (!rknpu_obj->cookie) {
		/*
		 * when RKNPU_MEM_CONTIGUOUS and IOMMU is available
		 * try to fallback to allocate non-contiguous buffer
		 */
		if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
		    rknpu_dev->iommu_en) {
			LOG_DEV_WARN(
				drm->dev,
				"try to fallback to allocate non-contiguous %lu buffer.\n",
				rknpu_obj->size);
			rknpu_obj->dma_attrs &= ~DMA_ATTR_FORCE_CONTIGUOUS;
			rknpu_obj->flags |= RKNPU_MEM_NON_CONTIGUOUS;
			rknpu_obj->cookie =
				dma_alloc_attrs(drm->dev, rknpu_obj->size,
						&rknpu_obj->dma_addr, gfp_mask,
						rknpu_obj->dma_attrs);
			if (!rknpu_obj->cookie) {
				LOG_DEV_ERROR(
					drm->dev,
					"failed to allocate non-contiguous %lu buffer.\n",
					rknpu_obj->size);
				goto err_free;
			}
		} else {
			LOG_DEV_ERROR(drm->dev,
				      "failed to allocate %lu buffer.\n",
				      rknpu_obj->size);
			goto err_free;
		}
	}

	if (rknpu_obj->flags & RKNPU_MEM_KERNEL_MAPPING)
		rknpu_obj->kv_addr = rknpu_obj->cookie;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err_free_dma;
	}

	ret = dma_get_sgtable_attrs(drm->dev, sgt, rknpu_obj->cookie,
				    rknpu_obj->dma_addr, rknpu_obj->size,
				    rknpu_obj->dma_attrs);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to get sgtable.\n");
		goto err_free_sgt;
	}

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		sg_dma_address(s) = sg_phys(s);
		LOG_DEBUG("dma alloc sgt[%d], phys_address: %pad, length: %u\n",
			  i, &s->dma_address, s->length);
	}

	if (drm_prime_sg_to_page_addr_arrays(sgt, rknpu_obj->pages, NULL,
					     nr_pages)) {
		LOG_DEV_ERROR(drm->dev, "invalid sgtable.\n");
		ret = -EINVAL;
		goto err_free_sg_table;
	}

	rknpu_obj->sgt = sgt;

	return ret;

err_free_sg_table:
	sg_free_table(sgt);
err_free_sgt:
	kfree(sgt);
err_free_dma:
	dma_free_attrs(drm->dev, rknpu_obj->size, rknpu_obj->cookie,
		       rknpu_obj->dma_addr, rknpu_obj->dma_attrs);
err_free:
	rknpu_gem_free_page(rknpu_obj->pages);

	return ret;
}

static void rknpu_gem_free_buf(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
#if RKNPU_GEM_ALLOC_FROM_PAGES
	struct rknpu_device *rknpu_dev = drm->dev_private;
#endif

	if (!rknpu_obj->dma_addr) {
		LOG_DEBUG("dma handle is invalid.\n");
		return;
	}

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		rknpu_gem_put_pages(rknpu_obj);
		return;
	}
#endif

	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);

	dma_free_attrs(drm->dev, rknpu_obj->size, rknpu_obj->cookie,
		       rknpu_obj->dma_addr, rknpu_obj->dma_attrs);

	rknpu_gem_free_page(rknpu_obj->pages);

	rknpu_obj->dma_addr = 0;
}

static int rknpu_gem_handle_create(struct drm_gem_object *obj,
				   struct drm_file *file_priv,
				   unsigned int *handle)
{
	int ret = -EINVAL;
	/*
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file_priv, obj, handle);
	if (ret)
		return ret;

	LOG_DEBUG("gem handle: %#x\n", *handle);

	/* drop reference from allocate - handle holds it now. */
	rknpu_gem_object_put(obj);

	return 0;
}

static int rknpu_gem_handle_destroy(struct drm_file *file_priv,
				    unsigned int handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

static struct rknpu_gem_object *rknpu_gem_init(struct drm_device *drm,
					       unsigned long size)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct drm_gem_object *obj = NULL;
	gfp_t gfp_mask;
	int ret = -EINVAL;

	rknpu_obj = kzalloc(sizeof(*rknpu_obj), GFP_KERNEL);
	if (!rknpu_obj)
		return ERR_PTR(-ENOMEM);

	obj = &rknpu_obj->base;

	ret = drm_gem_object_init(drm, obj, size);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to initialize gem object\n");
		kfree(rknpu_obj);
		return ERR_PTR(ret);
	}

	rknpu_obj->size = rknpu_obj->base.size;

	gfp_mask = mapping_gfp_mask(obj->filp->f_mapping);

	if (rknpu_obj->flags & RKNPU_MEM_ZEROING)
		gfp_mask |= __GFP_ZERO;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_DMA32)) {
		gfp_mask &= ~__GFP_HIGHMEM;
		gfp_mask |= __GFP_DMA32;
	}

	mapping_set_gfp_mask(obj->filp->f_mapping, gfp_mask);

	return rknpu_obj;
}

static void rknpu_gem_release(struct rknpu_gem_object *rknpu_obj)
{
	/* release file pointer to gem object. */
	drm_gem_object_release(&rknpu_obj->base);
	kfree(rknpu_obj);
}

static int rknpu_gem_alloc_buf_with_sram(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct iommu_domain *domain = NULL;
	struct rknpu_iommu_dma_cookie *cookie = NULL;
	struct iova_domain *iovad = NULL;
	struct scatterlist *s = NULL;
	unsigned long length = 0;
	unsigned long size = 0;
	unsigned long offset = 0;
	int i = 0;
	int ret = -EINVAL;

	/* iova map to sram */
	domain = iommu_get_domain_for_dev(rknpu_dev->dev);
	if (!domain) {
		LOG_ERROR("failed to get iommu domain!");
		return -EINVAL;
	}

	cookie = domain->iova_cookie;
	iovad = &cookie->iovad;
	rknpu_obj->iova_size =
		iova_align(iovad, rknpu_obj->sram_size + rknpu_obj->size);
	rknpu_obj->iova_start = rknpu_iommu_dma_alloc_iova(
		domain, rknpu_obj->iova_size, dma_get_mask(drm->dev), drm->dev);
	if (!rknpu_obj->iova_start) {
		LOG_ERROR("iommu_dma_alloc_iova failed\n");
		return -ENOMEM;
	}

	LOG_INFO("allocate iova start: %pad, size: %lu\n",
		 &rknpu_obj->iova_start, rknpu_obj->iova_size);

	/*
	 * Overview SRAM + DDR map to IOVA
	 * --------
	 * sram_size: rknpu_obj->sram_size
	 *   - allocate from SRAM, this size value has been page-aligned
	 * size: rknpu_obj->size
	 *   - allocate from DDR pages, this size value has been page-aligned
	 * iova_size: rknpu_obj->iova_size
	 *   - from iova_align(sram_size + size)
	 *   - it may be larger than the (sram_size + size), and the larger part is not mapped
	 * --------
	 *
	 * |<- sram_size ->|      |<- - - - size - - - ->|
	 * +---------------+      +----------------------+
	 * |     SRAM      |      |         DDR          |
	 * +---------------+      +----------------------+
	 *         |                    |
	 * |       V       |            V          |
	 * +---------------------------------------+
	 * |             IOVA range                |
	 * +---------------------------------------+
	 * |<- - - - - - - iova_size - - - - - - ->|
	 *
	 */
	offset = rknpu_obj->sram_obj->range_start *
		 rknpu_dev->sram_mm->chunk_size;
	ret = iommu_map(domain, rknpu_obj->iova_start,
			rknpu_dev->sram_start + offset, rknpu_obj->sram_size,
			IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		LOG_ERROR("sram iommu_map error: %d\n", ret);
		goto free_iova;
	}

	rknpu_obj->dma_addr = rknpu_obj->iova_start;

	if (rknpu_obj->size == 0) {
		LOG_INFO("allocate sram size: %lu\n", rknpu_obj->sram_size);
		return 0;
	}

	rknpu_obj->pages = drm_gem_get_pages(&rknpu_obj->base);
	if (IS_ERR(rknpu_obj->pages)) {
		ret = PTR_ERR(rknpu_obj->pages);
		LOG_ERROR("failed to get pages: %d\n", ret);
		goto sram_unmap;
	}

	rknpu_obj->num_pages = rknpu_obj->size >> PAGE_SHIFT;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	rknpu_obj->sgt = drm_prime_pages_to_sg(drm, rknpu_obj->pages,
					       rknpu_obj->num_pages);
#else
	rknpu_obj->sgt =
		drm_prime_pages_to_sg(rknpu_obj->pages, rknpu_obj->num_pages);
#endif
	if (IS_ERR(rknpu_obj->sgt)) {
		ret = PTR_ERR(rknpu_obj->sgt);
		LOG_ERROR("failed to allocate sgt: %d\n", ret);
		goto put_pages;
	}

	length = rknpu_obj->size;
	offset = rknpu_obj->iova_start + rknpu_obj->sram_size;

	for_each_sg(rknpu_obj->sgt->sgl, s, rknpu_obj->sgt->nents, i) {
		size = (length < s->length) ? length : s->length;

		ret = iommu_map(domain, offset, sg_phys(s), size,
				IOMMU_READ | IOMMU_WRITE);
		if (ret) {
			LOG_ERROR("ddr iommu_map error: %d\n", ret);
			goto sgl_unmap;
		}

		length -= size;
		offset += size;

		if (length == 0)
			break;
	}

	LOG_INFO("allocate size: %lu with sram size: %lu\n", rknpu_obj->size,
		 rknpu_obj->sram_size);

	return 0;

sgl_unmap:
	iommu_unmap(domain, rknpu_obj->iova_start + rknpu_obj->sram_size,
		    rknpu_obj->size - length);
	sg_free_table(rknpu_obj->sgt);
	kfree(rknpu_obj->sgt);

put_pages:
	drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, false, false);

sram_unmap:
	iommu_unmap(domain, rknpu_obj->iova_start, rknpu_obj->sram_size);

free_iova:
	rknpu_iommu_dma_free_iova(domain->iova_cookie, rknpu_obj->iova_start,
				  rknpu_obj->iova_size);

	return ret;
}

static void rknpu_gem_free_buf_with_sram(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct iommu_domain *domain = NULL;

	domain = iommu_get_domain_for_dev(rknpu_dev->dev);
	if (domain) {
		iommu_unmap(domain, rknpu_obj->iova_start,
			    rknpu_obj->sram_size);
		if (rknpu_obj->size > 0)
			iommu_unmap(domain,
				    rknpu_obj->iova_start +
					    rknpu_obj->sram_size,
				    rknpu_obj->size);
		rknpu_iommu_dma_free_iova(domain->iova_cookie,
					  rknpu_obj->iova_start,
					  rknpu_obj->iova_size);
	}

	if (rknpu_obj->pages)
		drm_gem_put_pages(&rknpu_obj->base, rknpu_obj->pages, true,
				  true);

	if (rknpu_obj->sgt != NULL) {
		sg_free_table(rknpu_obj->sgt);
		kfree(rknpu_obj->sgt);
	}
}

struct rknpu_gem_object *rknpu_gem_object_create(struct drm_device *drm,
						 unsigned int flags,
						 unsigned long size,
						 unsigned long sram_size)
{
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct rknpu_gem_object *rknpu_obj = NULL;
	size_t remain_ddr_size = 0;
	int ret = -EINVAL;

	if (!size) {
		LOG_DEV_ERROR(drm->dev, "invalid buffer size: %lu\n", size);
		return ERR_PTR(-EINVAL);
	}

	remain_ddr_size = round_up(size, PAGE_SIZE);

	if (!rknpu_dev->iommu_en && (flags & RKNPU_MEM_NON_CONTIGUOUS)) {
		/*
		 * when no IOMMU is available, all allocated buffers are
		 * contiguous anyway, so drop RKNPU_MEM_NON_CONTIGUOUS flag
		 */
		flags &= ~RKNPU_MEM_NON_CONTIGUOUS;
		LOG_WARN(
			"non-contiguous allocation is not supported without IOMMU, falling back to contiguous buffer\n");
	}

	if (IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) &&
	    (flags & RKNPU_MEM_TRY_ALLOC_SRAM) && rknpu_dev->sram_size > 0) {
		size_t sram_free_size = 0;
		size_t real_sram_size = 0;

		if (sram_size != 0)
			sram_size = round_up(sram_size, PAGE_SIZE);

		rknpu_obj = rknpu_gem_init(drm, remain_ddr_size);
		if (IS_ERR(rknpu_obj))
			return rknpu_obj;

		/* set memory type and cache attribute from user side. */
		rknpu_obj->flags = flags;

		sram_free_size = rknpu_dev->sram_mm->free_chunks *
				 rknpu_dev->sram_mm->chunk_size;
		if (sram_free_size > 0) {
			real_sram_size = remain_ddr_size;
			if (sram_size != 0 && remain_ddr_size > sram_size)
				real_sram_size = sram_size;
			if (real_sram_size > sram_free_size)
				real_sram_size = sram_free_size;
			ret = rknpu_mm_alloc(rknpu_dev->sram_mm, real_sram_size,
					     &rknpu_obj->sram_obj);
			if (ret != 0) {
				sram_free_size =
					rknpu_dev->sram_mm->free_chunks *
					rknpu_dev->sram_mm->chunk_size;
				LOG_WARN(
					"mm allocate %zu failed, ret: %d, free size: %zu\n",
					real_sram_size, ret, sram_free_size);
				real_sram_size = 0;
			}
		}

		if (real_sram_size > 0) {
			rknpu_obj->sram_size = real_sram_size;

			ret = rknpu_gem_alloc_buf_with_sram(rknpu_obj);
			if (ret < 0)
				goto mm_free;
			remain_ddr_size = 0;
		}
	}

	if (remain_ddr_size > 0) {
		rknpu_obj = rknpu_gem_init(drm, remain_ddr_size);
		if (IS_ERR(rknpu_obj))
			return rknpu_obj;

		/* set memory type and cache attribute from user side. */
		rknpu_obj->flags = flags;

		ret = rknpu_gem_alloc_buf(rknpu_obj);
		if (ret < 0)
			goto gem_release;
	}

	if (rknpu_obj)
		LOG_DEBUG(
			"created dma addr: %pad, cookie: %p, ddr size: %lu, sram size: %lu, attrs: %#lx, flags: %#x\n",
			&rknpu_obj->dma_addr, rknpu_obj->cookie, rknpu_obj->size,
			rknpu_obj->sram_size, rknpu_obj->dma_attrs, rknpu_obj->flags);

	return rknpu_obj;

mm_free:
	if (IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) &&
	    rknpu_obj->sram_obj != NULL)
		rknpu_mm_free(rknpu_dev->sram_mm, rknpu_obj->sram_obj);

gem_release:
	rknpu_gem_release(rknpu_obj);

	return ERR_PTR(ret);
}

void rknpu_gem_object_destroy(struct rknpu_gem_object *rknpu_obj)
{
	struct drm_gem_object *obj = &rknpu_obj->base;

	LOG_DEBUG(
		"destroy dma addr: %pad, cookie: %p, size: %lu, attrs: %#lx, flags: %#x, handle count: %d\n",
		&rknpu_obj->dma_addr, rknpu_obj->cookie, rknpu_obj->size,
		rknpu_obj->dma_attrs, rknpu_obj->flags, obj->handle_count);

	/*
	 * do not release memory region from exporter.
	 *
	 * the region will be released by exporter
	 * once dmabuf's refcount becomes 0.
	 */
	if (obj->import_attach) {
		drm_prime_gem_destroy(obj, rknpu_obj->sgt);
		rknpu_gem_free_page(rknpu_obj->pages);
	} else {
		if (IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) &&
		    rknpu_obj->sram_size > 0) {
			struct rknpu_device *rknpu_dev = obj->dev->dev_private;

			if (rknpu_obj->sram_obj != NULL)
				rknpu_mm_free(rknpu_dev->sram_mm,
					      rknpu_obj->sram_obj);
			rknpu_gem_free_buf_with_sram(rknpu_obj);
		} else {
			rknpu_gem_free_buf(rknpu_obj);
		}
	}

	rknpu_gem_release(rknpu_obj);
}

int rknpu_gem_create_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv)
{
	struct rknpu_mem_create *args = data;
	struct rknpu_gem_object *rknpu_obj = NULL;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_object_find(file_priv, args->handle);
	if (!rknpu_obj) {
		rknpu_obj = rknpu_gem_object_create(
			dev, args->flags, args->size, args->sram_size);
		if (IS_ERR(rknpu_obj))
			return PTR_ERR(rknpu_obj);

		ret = rknpu_gem_handle_create(&rknpu_obj->base, file_priv,
					      &args->handle);
		if (ret) {
			rknpu_gem_object_destroy(rknpu_obj);
			return ret;
		}
	}

	// rknpu_gem_object_get(&rknpu_obj->base);

	args->size = rknpu_obj->size;
	args->sram_size = rknpu_obj->sram_size;
	args->obj_addr = (__u64)(uintptr_t)rknpu_obj;
	args->dma_addr = rknpu_obj->dma_addr;

	return 0;
}

int rknpu_gem_map_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct rknpu_mem_map *args = data;

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
	return rknpu_gem_dumb_map_offset(file_priv, dev, args->handle,
					 &args->offset);
#else
	return drm_gem_dumb_map_offset(file_priv, dev, args->handle,
				       &args->offset);
#endif
}

int rknpu_gem_destroy_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct rknpu_mem_destroy *args = data;

	rknpu_obj = rknpu_gem_object_find(file_priv, args->handle);
	if (!rknpu_obj)
		return -EINVAL;

	// rknpu_gem_object_put(&rknpu_obj->base);

	return rknpu_gem_handle_destroy(file_priv, args->handle);
}

#if RKNPU_GEM_ALLOC_FROM_PAGES
/*
 * __vm_map_pages - maps range of kernel pages into user vma
 * @vma: user vma to map to
 * @pages: pointer to array of source kernel pages
 * @num: number of pages in page array
 * @offset: user's requested vm_pgoff
 *
 * This allows drivers to map range of kernel pages into a user vma.
 *
 * Return: 0 on success and error code otherwise.
 */
static int __vm_map_pages(struct vm_area_struct *vma, struct page **pages,
			  unsigned long num, unsigned long offset)
{
	unsigned long count = vma_pages(vma);
	unsigned long uaddr = vma->vm_start;
	int ret = -EINVAL, i = 0;

	/* Fail if the user requested offset is beyond the end of the object */
	if (offset >= num)
		return -ENXIO;

	/* Fail if the user requested size exceeds available object size */
	if (count > num - offset)
		return -ENXIO;

	for (i = 0; i < count; i++) {
		ret = vm_insert_page(vma, uaddr, pages[offset + i]);
		if (ret < 0)
			return ret;
		uaddr += PAGE_SIZE;
	}

	return 0;
}

static int rknpu_gem_mmap_pages(struct rknpu_gem_object *rknpu_obj,
				struct vm_area_struct *vma)
{
	struct drm_device *drm = rknpu_obj->base.dev;
	int ret = -EINVAL;

	vma->vm_flags |= VM_MIXEDMAP;

	ret = __vm_map_pages(vma, rknpu_obj->pages, rknpu_obj->num_pages,
			     vma->vm_pgoff);
	if (ret < 0)
		LOG_DEV_ERROR(drm->dev, "failed to map pages into vma: %d\n",
			      ret);

	return ret;
}
#endif

static int rknpu_gem_mmap_buffer(struct rknpu_gem_object *rknpu_obj,
				 struct vm_area_struct *vma)
{
	struct drm_device *drm = rknpu_obj->base.dev;
#if RKNPU_GEM_ALLOC_FROM_PAGES
	struct rknpu_device *rknpu_dev = drm->dev_private;
#endif
	unsigned long vm_size = 0;
	int ret = -EINVAL;

	/*
	 * clear the VM_PFNMAP flag that was set by drm_gem_mmap(), and set the
	 * vm_pgoff (used as a fake buffer offset by DRM) to 0 as we want to map
	 * the whole buffer.
	 */
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_pgoff = 0;

	vm_size = vma->vm_end - vma->vm_start;

	/* check if user-requested size is valid. */
	if (vm_size > rknpu_obj->size)
		return -EINVAL;

	if (rknpu_obj->sram_size > 0) {
		unsigned long offset = 0;
		unsigned long num_pages = 0;
		int i = 0;

		vma->vm_flags |= VM_MIXEDMAP;

		offset = rknpu_obj->sram_obj->range_start *
			 rknpu_dev->sram_mm->chunk_size;
		vma->vm_pgoff = __phys_to_pfn(rknpu_dev->sram_start + offset);

		ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				      rknpu_obj->sram_size, vma->vm_page_prot);
		if (ret)
			return -EAGAIN;

		if (rknpu_obj->size == 0)
			return 0;

		offset = rknpu_obj->sram_size;

		num_pages = (vm_size - rknpu_obj->sram_size) / PAGE_SIZE;
		for (i = 0; i < num_pages; ++i) {
			ret = vm_insert_page(vma, vma->vm_start + offset,
					     rknpu_obj->pages[i]);
			if (ret < 0)
				return ret;
			offset += PAGE_SIZE;
		}

		return 0;
	}

#if RKNPU_GEM_ALLOC_FROM_PAGES
	if ((rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS) &&
	    rknpu_dev->iommu_en) {
		return rknpu_gem_mmap_pages(rknpu_obj, vma);
	}
#endif

	ret = dma_mmap_attrs(drm->dev, vma, rknpu_obj->cookie,
			     rknpu_obj->dma_addr, rknpu_obj->size,
			     rknpu_obj->dma_attrs);
	if (ret < 0) {
		LOG_DEV_ERROR(drm->dev, "failed to mmap, ret: %d\n", ret);
		return ret;
	}

	return 0;
}

void rknpu_gem_free_object(struct drm_gem_object *obj)
{
	rknpu_gem_object_destroy(to_rknpu_obj(obj));
}

int rknpu_gem_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
			  struct drm_mode_create_dumb *args)
{
	struct rknpu_device *rknpu_dev = drm->dev_private;
	struct rknpu_gem_object *rknpu_obj = NULL;
	unsigned int flags = 0;
	int ret = -EINVAL;

	/*
	 * allocate memory to be used for framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_CREATE_DUMB command.
	 */
	args->pitch = args->width * ((args->bpp + 7) / 8);
	args->size = args->pitch * args->height;

	if (rknpu_dev->iommu_en)
		flags = RKNPU_MEM_NON_CONTIGUOUS | RKNPU_MEM_WRITE_COMBINE;
	else
		flags = RKNPU_MEM_CONTIGUOUS | RKNPU_MEM_WRITE_COMBINE;

	rknpu_obj = rknpu_gem_object_create(drm, flags, args->size, 0);
	if (IS_ERR(rknpu_obj)) {
		LOG_DEV_ERROR(drm->dev, "gem object allocate failed.\n");
		return PTR_ERR(rknpu_obj);
	}

	ret = rknpu_gem_handle_create(&rknpu_obj->base, file_priv,
				      &args->handle);
	if (ret) {
		rknpu_gem_object_destroy(rknpu_obj);
		return ret;
	}

	return 0;
}

#if KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE
int rknpu_gem_dumb_map_offset(struct drm_file *file_priv,
			      struct drm_device *drm, uint32_t handle,
			      uint64_t *offset)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct drm_gem_object *obj = NULL;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_object_find(file_priv, handle);
	if (!rknpu_obj)
		return 0;

	/* Don't allow imported objects to be mapped */
	obj = &rknpu_obj->base;
	if (obj->import_attach)
		return -EINVAL;

	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		return ret;

	*offset = drm_vma_node_offset_addr(&obj->vma_node);

	return 0;
}
#endif

#if KERNEL_VERSION(4, 15, 0) <= LINUX_VERSION_CODE
vm_fault_t rknpu_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;

	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		return VM_FAULT_SIGBUS;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	return vmf_insert_mixed(vma, vmf->address,
				__pfn_to_pfn_t(pfn, PFN_DEV));
}
#elif KERNEL_VERSION(4, 14, 0) <= LINUX_VERSION_CODE
int rknpu_gem_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;
	int ret = -EINVAL;

	page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	ret = vm_insert_mixed(vma, vmf->address, __pfn_to_pfn_t(pfn, PFN_DEV));

out:
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}
#else
int rknpu_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	struct drm_device *drm = rknpu_obj->base.dev;
	unsigned long pfn = 0;
	pgoff_t page_offset = 0;
	int ret = -EINVAL;

	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		      PAGE_SHIFT;

	if (page_offset >= (rknpu_obj->size >> PAGE_SHIFT)) {
		LOG_DEV_ERROR(drm->dev, "invalid page offset\n");
		ret = -EINVAL;
		goto out;
	}

	pfn = page_to_pfn(rknpu_obj->pages[page_offset]);
	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address,
			      __pfn_to_pfn_t(pfn, PFN_DEV));

out:
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	case -ENOMEM:
		return VM_FAULT_OOM;
	default:
		return VM_FAULT_SIGBUS;
	}
}
#endif

static int rknpu_gem_mmap_obj(struct drm_gem_object *obj,
			      struct vm_area_struct *vma)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	int ret = -EINVAL;

	LOG_DEBUG("flags: %#x\n", rknpu_obj->flags);

	/* non-cacheable as default. */
	if (rknpu_obj->flags & RKNPU_MEM_CACHEABLE) {
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	} else if (rknpu_obj->flags & RKNPU_MEM_WRITE_COMBINE) {
		vma->vm_page_prot =
			pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else {
		vma->vm_page_prot =
			pgprot_noncached(vm_get_page_prot(vma->vm_flags));
	}

	ret = rknpu_gem_mmap_buffer(rknpu_obj, vma);
	if (ret)
		goto err_close_vm;

	return 0;

err_close_vm:
	drm_gem_vm_close(vma);

	return ret;
}

int rknpu_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_gem_object *obj = NULL;
	int ret = -EINVAL;

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		LOG_ERROR("failed to mmap, ret: %d\n", ret);
		return ret;
	}

	obj = vma->vm_private_data;

	if (obj->import_attach)
		return dma_buf_mmap(obj->dma_buf, vma, 0);

	return rknpu_gem_mmap_obj(obj, vma);
}

/* low-level interface prime helpers */
#if KERNEL_VERSION(4, 13, 0) <= LINUX_VERSION_CODE
struct drm_gem_object *rknpu_gem_prime_import(struct drm_device *dev,
					      struct dma_buf *dma_buf)
{
	return drm_gem_prime_import_dev(dev, dma_buf, dev->dev);
}
#endif

struct sg_table *rknpu_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);
	int npages = 0;

	npages = rknpu_obj->size >> PAGE_SHIFT;

#if KERNEL_VERSION(5, 10, 0) <= LINUX_VERSION_CODE
	return drm_prime_pages_to_sg(obj->dev, rknpu_obj->pages, npages);
#else
	return drm_prime_pages_to_sg(rknpu_obj->pages, npages);
#endif
}

struct drm_gem_object *
rknpu_gem_prime_import_sg_table(struct drm_device *dev,
				struct dma_buf_attachment *attach,
				struct sg_table *sgt)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	int npages = 0;
	int ret = -EINVAL;

	rknpu_obj = rknpu_gem_init(dev, attach->dmabuf->size);
	if (IS_ERR(rknpu_obj)) {
		ret = PTR_ERR(rknpu_obj);
		return ERR_PTR(ret);
	}

	rknpu_obj->dma_addr = sg_dma_address(sgt->sgl);

	npages = rknpu_obj->size >> PAGE_SHIFT;
	rknpu_obj->pages = rknpu_gem_alloc_page(npages);
	if (!rknpu_obj->pages) {
		ret = -ENOMEM;
		goto err;
	}

	ret = drm_prime_sg_to_page_addr_arrays(sgt, rknpu_obj->pages, NULL,
					       npages);
	if (ret < 0)
		goto err_free_large;

	rknpu_obj->sgt = sgt;

	if (sgt->nents == 1) {
		/* always physically continuous memory if sgt->nents is 1. */
		rknpu_obj->flags |= RKNPU_MEM_CONTIGUOUS;
	} else {
		/*
		 * this case could be CONTIG or NONCONTIG type but for now
		 * sets NONCONTIG.
		 * TODO. we have to find a way that exporter can notify
		 * the type of its own buffer to importer.
		 */
		rknpu_obj->flags |= RKNPU_MEM_NON_CONTIGUOUS;
	}

	return &rknpu_obj->base;

err_free_large:
	rknpu_gem_free_page(rknpu_obj->pages);
err:
	rknpu_gem_release(rknpu_obj);
	return ERR_PTR(ret);
}

void *rknpu_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct rknpu_gem_object *rknpu_obj = to_rknpu_obj(obj);

	if (!rknpu_obj->pages)
		return NULL;

	return vmap(rknpu_obj->pages, rknpu_obj->num_pages, VM_MAP,
		    PAGE_KERNEL);
}

void rknpu_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	vunmap(vaddr);
}

int rknpu_gem_prime_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	int ret = -EINVAL;

	ret = drm_gem_mmap_obj(obj, obj->size, vma);
	if (ret < 0)
		return ret;

	return rknpu_gem_mmap_obj(obj, vma);
}

int rknpu_gem_sync_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct rknpu_gem_object *rknpu_obj = NULL;
	struct rknpu_mem_sync *args = data;
	struct scatterlist *sg;
	unsigned long length, offset = 0;
	unsigned long sg_left, size = 0;
	unsigned long len = 0;
	int i;

	rknpu_obj = (struct rknpu_gem_object *)(uintptr_t)args->obj_addr;
	if (!rknpu_obj)
		return -EINVAL;

	if (!(rknpu_obj->flags & RKNPU_MEM_CACHEABLE))
		return -EINVAL;

	if (!(rknpu_obj->flags & RKNPU_MEM_NON_CONTIGUOUS)) {
		if (args->flags & RKNPU_MEM_SYNC_TO_DEVICE) {
			dma_sync_single_range_for_device(
				dev->dev, rknpu_obj->dma_addr, args->offset,
				args->size, DMA_TO_DEVICE);
		}
		if (args->flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
			dma_sync_single_range_for_cpu(dev->dev,
						      rknpu_obj->dma_addr,
						      args->offset, args->size,
						      DMA_FROM_DEVICE);
		}
	} else {
		length = args->size;
		offset = args->offset;

		if (IS_ENABLED(CONFIG_ROCKCHIP_RKNPU_SRAM) && rknpu_obj->sram_size > 0) {
			struct drm_gem_object *obj = &rknpu_obj->base;
			struct rknpu_device *rknpu_dev = obj->dev->dev_private;
			unsigned long sram_offset =
				rknpu_obj->sram_obj->range_start *
				rknpu_dev->sram_mm->chunk_size;
			if ((offset + length) <= rknpu_obj->sram_size) {
				__dma_map_area(rknpu_dev->sram_base_io +
						       offset + sram_offset,
					       length, DMA_TO_DEVICE);
				__dma_unmap_area(rknpu_dev->sram_base_io +
							 offset + sram_offset,
						 length, DMA_FROM_DEVICE);
				length = 0;
				offset = 0;
			} else if (offset >= rknpu_obj->sram_size) {
				offset -= rknpu_obj->sram_size;
			} else {
				unsigned long sram_length =
					rknpu_obj->sram_size - offset;
				__dma_map_area(rknpu_dev->sram_base_io +
						       offset + sram_offset,
					       sram_length, DMA_TO_DEVICE);
				__dma_unmap_area(rknpu_dev->sram_base_io +
							 offset + sram_offset,
						 sram_length, DMA_FROM_DEVICE);
				length -= sram_length;
				offset = 0;
			}
		}

		for_each_sg(rknpu_obj->sgt->sgl, sg, rknpu_obj->sgt->nents,
			     i) {
			if (length == 0)
				break;

			len += sg->length;
			if (len <= offset)
				continue;

			sg_left = len - offset;
			size = (length < sg_left) ? length : sg_left;

			if (args->flags & RKNPU_MEM_SYNC_TO_DEVICE) {
				dma_sync_sg_for_device(dev->dev, sg, 1,
						       DMA_TO_DEVICE);
			}

			if (args->flags & RKNPU_MEM_SYNC_FROM_DEVICE) {
				dma_sync_sg_for_cpu(dev->dev, sg, 1,
						    DMA_FROM_DEVICE);
			}

			offset += size;
			length -= size;
		}
	}

	return 0;
}
