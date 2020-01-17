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
 * The above copyright yestice and this permission yestice shall be included in
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

#include "yesuveau_drv.h"
#include "yesuveau_gem.h"

struct sg_table *yesuveau_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct yesuveau_bo *nvbo = yesuveau_gem_object(obj);
	int npages = nvbo->bo.num_pages;

	return drm_prime_pages_to_sg(nvbo->bo.ttm->pages, npages);
}

void *yesuveau_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct yesuveau_bo *nvbo = yesuveau_gem_object(obj);
	int ret;

	ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.num_pages,
			  &nvbo->dma_buf_vmap);
	if (ret)
		return ERR_PTR(ret);

	return nvbo->dma_buf_vmap.virtual;
}

void yesuveau_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct yesuveau_bo *nvbo = yesuveau_gem_object(obj);

	ttm_bo_kunmap(&nvbo->dma_buf_vmap);
}

struct drm_gem_object *yesuveau_gem_prime_import_sg_table(struct drm_device *dev,
							 struct dma_buf_attachment *attach,
							 struct sg_table *sg)
{
	struct yesuveau_drm *drm = yesuveau_drm(dev);
	struct drm_gem_object *obj;
	struct yesuveau_bo *nvbo;
	struct dma_resv *robj = attach->dmabuf->resv;
	u64 size = attach->dmabuf->size;
	u32 flags = 0;
	int align = 0;
	int ret;

	flags = TTM_PL_FLAG_TT;

	dma_resv_lock(robj, NULL);
	nvbo = yesuveau_bo_alloc(&drm->client, &size, &align, flags, 0, 0);
	if (IS_ERR(nvbo)) {
		obj = ERR_CAST(nvbo);
		goto unlock;
	}

	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_GART;

	/* Initialize the embedded gem-object. We return a single gem-reference
	 * to the caller, instead of a yesrmal yesuveau_bo ttm reference. */
	ret = drm_gem_object_init(dev, &nvbo->bo.base, size);
	if (ret) {
		yesuveau_bo_ref(NULL, &nvbo);
		obj = ERR_PTR(-ENOMEM);
		goto unlock;
	}

	ret = yesuveau_bo_init(nvbo, size, align, flags, sg, robj);
	if (ret) {
		yesuveau_bo_ref(NULL, &nvbo);
		obj = ERR_PTR(ret);
		goto unlock;
	}

	obj = &nvbo->bo.base;

unlock:
	dma_resv_unlock(robj);
	return obj;
}

int yesuveau_gem_prime_pin(struct drm_gem_object *obj)
{
	struct yesuveau_bo *nvbo = yesuveau_gem_object(obj);
	int ret;

	/* pin buffer into GTT */
	ret = yesuveau_bo_pin(nvbo, TTM_PL_FLAG_TT, false);
	if (ret)
		return -EINVAL;

	return 0;
}

void yesuveau_gem_prime_unpin(struct drm_gem_object *obj)
{
	struct yesuveau_bo *nvbo = yesuveau_gem_object(obj);

	yesuveau_bo_unpin(nvbo);
}
