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

#include <drm/drmP.h>

#include "nouveau_drm.h"
#include "nouveau_gem.h"

struct sg_table *nouveau_gem_prime_get_sg_table(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);
	int npages = nvbo->bo.num_pages;

	return drm_prime_pages_to_sg(nvbo->bo.ttm->pages, npages);
}

void *nouveau_gem_prime_vmap(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);
	int ret;

	ret = ttm_bo_kmap(&nvbo->bo, 0, nvbo->bo.num_pages,
			  &nvbo->dma_buf_vmap);
	if (ret)
		return ERR_PTR(ret);

	return nvbo->dma_buf_vmap.virtual;
}

void nouveau_gem_prime_vunmap(struct drm_gem_object *obj, void *vaddr)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);

	ttm_bo_kunmap(&nvbo->dma_buf_vmap);
}

struct drm_gem_object *nouveau_gem_prime_import_sg_table(struct drm_device *dev,
							 size_t size,
							 struct sg_table *sg)
{
	struct nouveau_bo *nvbo;
	u32 flags = 0;
	int ret;

	flags = TTM_PL_FLAG_TT;

	ret = nouveau_bo_new(dev, size, 0, flags, 0, 0,
			     sg, &nvbo);
	if (ret)
		return ERR_PTR(ret);

	nvbo->valid_domains = NOUVEAU_GEM_DOMAIN_GART;
	nvbo->gem = drm_gem_object_alloc(dev, nvbo->bo.mem.size);
	if (!nvbo->gem) {
		nouveau_bo_ref(NULL, &nvbo);
		return ERR_PTR(-ENOMEM);
	}

	nvbo->gem->driver_private = nvbo;
	return nvbo->gem;
}

int nouveau_gem_prime_pin(struct drm_gem_object *obj)
{
	struct nouveau_bo *nvbo = nouveau_gem_object(obj);
	int ret = 0;

	/* pin buffer into GTT */
	ret = nouveau_bo_pin(nvbo, TTM_PL_FLAG_TT);
	if (ret)
		return -EINVAL;

	return 0;
}
