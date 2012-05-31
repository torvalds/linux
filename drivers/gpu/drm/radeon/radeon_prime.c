/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * based on nouveau_prime.c
 *
 * Authors: Alex Deucher
 */
#include "drmP.h"
#include "drm.h"

#include "radeon.h"
#include "radeon_drm.h"

#include <linux/dma-buf.h>

static struct sg_table *radeon_gem_map_dma_buf(struct dma_buf_attachment *attachment,
					       enum dma_data_direction dir)
{
	struct radeon_bo *bo = attachment->dmabuf->priv;
	struct drm_device *dev = bo->rdev->ddev;
	int npages = bo->tbo.num_pages;
	struct sg_table *sg;
	int nents;

	mutex_lock(&dev->struct_mutex);
	sg = drm_prime_pages_to_sg(bo->tbo.ttm->pages, npages);
	nents = dma_map_sg(attachment->dev, sg->sgl, sg->nents, dir);
	mutex_unlock(&dev->struct_mutex);
	return sg;
}

static void radeon_gem_unmap_dma_buf(struct dma_buf_attachment *attachment,
				     struct sg_table *sg, enum dma_data_direction dir)
{
	dma_unmap_sg(attachment->dev, sg->sgl, sg->nents, dir);
	sg_free_table(sg);
	kfree(sg);
}

static void radeon_gem_dmabuf_release(struct dma_buf *dma_buf)
{
	struct radeon_bo *bo = dma_buf->priv;

	if (bo->gem_base.export_dma_buf == dma_buf) {
		DRM_ERROR("unreference dmabuf %p\n", &bo->gem_base);
		bo->gem_base.export_dma_buf = NULL;
		drm_gem_object_unreference_unlocked(&bo->gem_base);
	}
}

static void *radeon_gem_kmap_atomic(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void radeon_gem_kunmap_atomic(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}
static void *radeon_gem_kmap(struct dma_buf *dma_buf, unsigned long page_num)
{
	return NULL;
}

static void radeon_gem_kunmap(struct dma_buf *dma_buf, unsigned long page_num, void *addr)
{

}

static int radeon_gem_prime_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static void *radeon_gem_prime_vmap(struct dma_buf *dma_buf)
{
	struct radeon_bo *bo = dma_buf->priv;
	struct drm_device *dev = bo->rdev->ddev;
	int ret;

	mutex_lock(&dev->struct_mutex);
	if (bo->vmapping_count) {
		bo->vmapping_count++;
		goto out_unlock;
	}

	ret = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages,
			  &bo->dma_buf_vmap);
	if (ret) {
		mutex_unlock(&dev->struct_mutex);
		return ERR_PTR(ret);
	}
	bo->vmapping_count = 1;
out_unlock:
	mutex_unlock(&dev->struct_mutex);
	return bo->dma_buf_vmap.virtual;
}

static void radeon_gem_prime_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct radeon_bo *bo = dma_buf->priv;
	struct drm_device *dev = bo->rdev->ddev;

	mutex_lock(&dev->struct_mutex);
	bo->vmapping_count--;
	if (bo->vmapping_count == 0) {
		ttm_bo_kunmap(&bo->dma_buf_vmap);
	}
	mutex_unlock(&dev->struct_mutex);
}
const static struct dma_buf_ops radeon_dmabuf_ops =  {
	.map_dma_buf = radeon_gem_map_dma_buf,
	.unmap_dma_buf = radeon_gem_unmap_dma_buf,
	.release = radeon_gem_dmabuf_release,
	.kmap = radeon_gem_kmap,
	.kmap_atomic = radeon_gem_kmap_atomic,
	.kunmap = radeon_gem_kunmap,
	.kunmap_atomic = radeon_gem_kunmap_atomic,
	.mmap = radeon_gem_prime_mmap,
	.vmap = radeon_gem_prime_vmap,
	.vunmap = radeon_gem_prime_vunmap,
};

static int radeon_prime_create(struct drm_device *dev,
			       size_t size,
			       struct sg_table *sg,
			       struct radeon_bo **pbo)
{
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_bo *bo;
	int ret;

	ret = radeon_bo_create(rdev, size, PAGE_SIZE, false,
			       RADEON_GEM_DOMAIN_GTT, sg, pbo);
	if (ret)
		return ret;
	bo = *pbo;
	bo->gem_base.driver_private = bo;

	mutex_lock(&rdev->gem.mutex);
	list_add_tail(&bo->list, &rdev->gem.objects);
	mutex_unlock(&rdev->gem.mutex);

	return 0;
}

struct dma_buf *radeon_gem_prime_export(struct drm_device *dev,
					struct drm_gem_object *obj,
					int flags)
{
	struct radeon_bo *bo = gem_to_radeon_bo(obj);
	int ret = 0;

	/* pin buffer into GTT */
	ret = radeon_bo_pin(bo, RADEON_GEM_DOMAIN_GTT, NULL);
	if (ret)
		return ERR_PTR(ret);

	return dma_buf_export(bo, &radeon_dmabuf_ops, obj->size, flags);
}

struct drm_gem_object *radeon_gem_prime_import(struct drm_device *dev,
					       struct dma_buf *dma_buf)
{
	struct dma_buf_attachment *attach;
	struct sg_table *sg;
	struct radeon_bo *bo;
	int ret;

	if (dma_buf->ops == &radeon_dmabuf_ops) {
		bo = dma_buf->priv;
		if (bo->gem_base.dev == dev) {
			drm_gem_object_reference(&bo->gem_base);
			return &bo->gem_base;
		}
	}

	/* need to attach */
	attach = dma_buf_attach(dma_buf, dev->dev);
	if (IS_ERR(attach))
		return ERR_CAST(attach);

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg)) {
		ret = PTR_ERR(sg);
		goto fail_detach;
	}

	ret = radeon_prime_create(dev, dma_buf->size, sg, &bo);
	if (ret)
		goto fail_unmap;

	bo->gem_base.import_attach = attach;

	return &bo->gem_base;

fail_unmap:
	dma_buf_unmap_attachment(attach, sg, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
	return ERR_PTR(ret);
}
