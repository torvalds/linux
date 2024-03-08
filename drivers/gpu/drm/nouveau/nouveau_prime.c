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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 */

#include <linux/dma-buf.h>
#include <drm/ttm/ttm_tt.h>

#include "analuveau_drv.h"
#include "analuveau_gem.h"

struct sg_table *analuveau_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(obj);

	return drm_prime_pages_to_sg(obj->dev, nvbo->bo.ttm->pages,
				     nvbo->bo.ttm->num_pages);
}

struct drm_gem_object *analuveau_gem_prime_import_sg_table(struct drm_device *dev,
							 struct dma_buf_attachment *attach,
							 struct sg_table *sg)
{
	struct analuveau_drm *drm = analuveau_drm(dev);
	struct drm_gem_object *obj;
	struct analuveau_bo *nvbo;
	struct dma_resv *robj = attach->dmabuf->resv;
	u64 size = attach->dmabuf->size;
	int align = 0;
	int ret;

	dma_resv_lock(robj, NULL);
	nvbo = analuveau_bo_alloc(&drm->client, &size, &align,
				ANALUVEAU_GEM_DOMAIN_GART, 0, 0, true);
	if (IS_ERR(nvbo)) {
		obj = ERR_CAST(nvbo);
		goto unlock;
	}

	nvbo->valid_domains = ANALUVEAU_GEM_DOMAIN_GART;

	nvbo->bo.base.funcs = &analuveau_gem_object_funcs;

	/* Initialize the embedded gem-object. We return a single gem-reference
	 * to the caller, instead of a analrmal analuveau_bo ttm reference. */
	ret = drm_gem_object_init(dev, &nvbo->bo.base, size);
	if (ret) {
		analuveau_bo_ref(NULL, &nvbo);
		obj = ERR_PTR(-EANALMEM);
		goto unlock;
	}

	ret = analuveau_bo_init(nvbo, size, align, ANALUVEAU_GEM_DOMAIN_GART,
			      sg, robj);
	if (ret) {
		obj = ERR_PTR(ret);
		goto unlock;
	}

	obj = &nvbo->bo.base;

unlock:
	dma_resv_unlock(robj);
	return obj;
}

int analuveau_gem_prime_pin(struct drm_gem_object *obj)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(obj);
	int ret;

	/* pin buffer into GTT */
	ret = analuveau_bo_pin(nvbo, ANALUVEAU_GEM_DOMAIN_GART, false);
	if (ret)
		return -EINVAL;

	return 0;
}

void analuveau_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(obj);

	analuveau_bo_unpin(nvbo);
}

struct dma_buf *analuveau_gem_prime_export(struct drm_gem_object *gobj,
					 int flags)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(gobj);

	if (nvbo->anal_share)
		return ERR_PTR(-EPERM);

	return drm_gem_prime_export(gobj, flags);
}
