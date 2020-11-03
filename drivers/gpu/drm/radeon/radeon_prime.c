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

#include <linux/dma-buf.h>

#include <drm/drm_prime.h>
#include <drm/radeon_drm.h>

#include "radeon.h"

struct sg_table *radeon_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct radeon_bo *bo = gem_to_radeon_bo(obj);
	int npages = bo->tbo.num_pages;

	return drm_prime_pages_to_sg(obj->dev, bo->tbo.ttm->pages, npages);
}

struct drm_gem_object *radeon_gem_prime_import_sg_table(struct drm_device *dev,
							struct dma_buf_attachment *attach,
							struct sg_table *sg)
{
	struct dma_resv *resv = attach->dmabuf->resv;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_bo *bo;
	int ret;

	dma_resv_lock(resv, NULL);
	ret = radeon_bo_create(rdev, attach->dmabuf->size, PAGE_SIZE, false,
			       RADEON_GEM_DOMAIN_GTT, 0, sg, resv, &bo);
	dma_resv_unlock(resv);
	if (ret)
		return ERR_PTR(ret);

	mutex_lock(&rdev->gem.mutex);
	list_add_tail(&bo->list, &rdev->gem.objects);
	mutex_unlock(&rdev->gem.mutex);

	bo->prime_shared_count = 1;
	return &bo->tbo.base;
}

int radeon_gem_prime_pin(struct drm_gem_object *obj)
{
	struct radeon_bo *bo = gem_to_radeon_bo(obj);
	int ret = 0;

	ret = radeon_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	/* pin buffer into GTT */
	ret = radeon_bo_pin(bo, RADEON_GEM_DOMAIN_GTT, NULL);
	if (likely(ret == 0))
		bo->prime_shared_count++;

	radeon_bo_unreserve(bo);
	return ret;
}

void radeon_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct radeon_bo *bo = gem_to_radeon_bo(obj);
	int ret = 0;

	ret = radeon_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return;

	radeon_bo_unpin(bo);
	if (bo->prime_shared_count)
		bo->prime_shared_count--;
	radeon_bo_unreserve(bo);
}


struct dma_buf *radeon_gem_prime_export(struct drm_gem_object *gobj,
					int flags)
{
	struct radeon_bo *bo = gem_to_radeon_bo(gobj);
	if (radeon_ttm_tt_has_userptr(bo->rdev, bo->tbo.ttm))
		return ERR_PTR(-EPERM);
	return drm_gem_prime_export(gobj, flags);
}
