/*
 * Copyright 2011 Red Hat Inc.
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
 * Authors: Dave Airlie
 */

#include <linux/dma-buf.h>

#include "nouveau_drv.h"
#include "nouveau_gem.h"

struct sg_table *nouveau_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);

	return drm_prime_pages_to_sg(obj->dev, nvbo->bo.ttm->pages,
				     nvbo->bo.ttm->num_pages);
}

struct drm_gem_object *nouveau_gem_prime_import_sg_table(struct drm_device *dev,
							 struct dma_buf_attachment *attach,
							 struct sg_table *sg)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_gem_object *obj;
	struct nouveau_bo *nvbo;
	struct dma_resv *robj = attach->dmabuf->resv;
	u64 size = attach->dmabuf->size;
	int align = 0;
	int ret;

	dma_resv_lock(robj, NULL);
	nvbo = nouveau_bo_alloc(&drm->client, &size, &align,
				NOUVEAU_GEM_DOMAIN_GART, 0, 0);
	if (IS_ERR(nvbo)) {
		obj = ERR_CAST(nvbo);
		goto unlock;
	}

	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_GART;

	nvbo->bo.base.funcs = &nouveau_gem_object_funcs;

	/* Initialize the embedded gem-object. We return a single gem-reference
	 * to the caller, instead of a normal nouveau_bo ttm reference. */
	ret = drm_gem_object_init(dev, &nvbo->bo.base, size);
	if (ret) {
		nouveau_bo_ref(NULL, &nvbo);
		obj = ERR_PTR(-ENOMEM);
		goto unlock;
	}

	ret = nouveau_bo_init(nvbo, size, align, NOUVEAU_GEM_DOMAIN_GART,
			      sg, robj);
	if (ret) {
		nouveau_bo_ref(NULL, &nvbo);
		obj = ERR_PTR(ret);
		goto unlock;
	}

	obj = &nvbo->bo.base;

unlock:
	dma_resv_unlock(robj);
	return obj;
}

int nouveau_gem_prime_pin(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);
	int ret;

	/* pin buffer into GTT */
	ret = nouveau_bo_pin(nvbo, NOUVEAU_GEM_DOMAIN_GART, false);
	if (ret)
		return -EINVAL;

	ret = ttm_bo_reserve(&nvbo->bo, false, false, NULL);
	if (ret)
		goto error;

	if (nvbo->bo.moving)
		ret = dma_fence_wait(nvbo->bo.moving, true);

	ttm_bo_unreserve(&nvbo->bo);
	if (ret)
		goto error;

	return ret;

error:
	nouveau_bo_unpin(nvbo);
	return ret;
}

void nouveau_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);

	nouveau_bo_unpin(nvbo);
}
