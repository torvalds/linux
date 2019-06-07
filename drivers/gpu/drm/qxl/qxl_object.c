/*
 * Copyright 2013 Red Hat Inc.
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
 *          Alon Levy
 */

#include "qxl_drv.h"
#include "qxl_object.h"

#include <linux/io-mapping.h>
static void qxl_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct qxl_bo *bo;
	struct qxl_device *qdev;

	bo = to_qxl_bo(tbo);
	qdev = (struct qxl_device *)bo->gem_base.dev->dev_private;

	qxl_surface_evict(qdev, bo, false);
	WARN_ON_ONCE(bo->map_count > 0);
	mutex_lock(&qdev->gem.mutex);
	list_del_init(&bo->list);
	mutex_unlock(&qdev->gem.mutex);
	drm_gem_object_release(&bo->gem_base);
	kfree(bo);
}

bool qxl_ttm_bo_is_qxl_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &qxl_ttm_bo_destroy)
		return true;
	return false;
}

void qxl_ttm_placement_from_domain(struct qxl_bo *qbo, u32 domain, bool pinned)
{
	u32 c = 0;
	u32 pflag = pinned ? TTM_PL_FLAG_NO_EVICT : 0;
	unsigned int i;

	qbo->placement.placement = qbo->placements;
	qbo->placement.busy_placement = qbo->placements;
	if (domain == QXL_GEM_DOMAIN_VRAM)
		qbo->placements[c++].flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM | pflag;
	if (domain == QXL_GEM_DOMAIN_SURFACE) {
		qbo->placements[c++].flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_PRIV | pflag;
		qbo->placements[c++].flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM | pflag;
	}
	if (domain == QXL_GEM_DOMAIN_CPU)
		qbo->placements[c++].flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM | pflag;
	if (!c)
		qbo->placements[c++].flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	qbo->placement.num_placement = c;
	qbo->placement.num_busy_placement = c;
	for (i = 0; i < c; ++i) {
		qbo->placements[i].fpfn = 0;
		qbo->placements[i].lpfn = 0;
	}
}

int qxl_bo_create(struct qxl_device *qdev,
		  unsigned long size, bool kernel, bool pinned, u32 domain,
		  struct qxl_surface *surf,
		  struct qxl_bo **bo_ptr)
{
	struct qxl_bo *bo;
	enum ttm_bo_type type;
	int r;

	if (kernel)
		type = ttm_bo_type_kernel;
	else
		type = ttm_bo_type_device;
	*bo_ptr = NULL;
	bo = kzalloc(sizeof(struct qxl_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	size = roundup(size, PAGE_SIZE);
	r = drm_gem_object_init(&qdev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->type = domain;
	bo->pin_count = pinned ? 1 : 0;
	bo->surface_id = 0;
	INIT_LIST_HEAD(&bo->list);

	if (surf)
		bo->surf = *surf;

	qxl_ttm_placement_from_domain(bo, domain, pinned);

	r = ttm_bo_init(&qdev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, 0, !kernel, size,
			NULL, NULL, &qxl_ttm_bo_destroy);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(qdev->ddev.dev,
				"object_init failed for (%lu, 0x%08X)\n",
				size, domain);
		return r;
	}
	*bo_ptr = bo;
	return 0;
}

int qxl_bo_kmap(struct qxl_bo *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->kptr) {
		if (ptr)
			*ptr = bo->kptr;
		bo->map_count++;
		return 0;
	}
	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;
	bo->kptr = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (ptr)
		*ptr = bo->kptr;
	bo->map_count = 1;
	return 0;
}

void *qxl_bo_kmap_atomic_page(struct qxl_device *qdev,
			      struct qxl_bo *bo, int page_offset)
{
	struct ttm_mem_type_manager *man = &bo->tbo.bdev->man[bo->tbo.mem.mem_type];
	void *rptr;
	int ret;
	struct io_mapping *map;

	if (bo->tbo.mem.mem_type == TTM_PL_VRAM)
		map = qdev->vram_mapping;
	else if (bo->tbo.mem.mem_type == TTM_PL_PRIV)
		map = qdev->surface_mapping;
	else
		goto fallback;

	(void) ttm_mem_io_lock(man, false);
	ret = ttm_mem_io_reserve(bo->tbo.bdev, &bo->tbo.mem);
	ttm_mem_io_unlock(man);

	return io_mapping_map_atomic_wc(map, bo->tbo.mem.bus.offset + page_offset);
fallback:
	if (bo->kptr) {
		rptr = bo->kptr + (page_offset * PAGE_SIZE);
		return rptr;
	}

	ret = qxl_bo_kmap(bo, &rptr);
	if (ret)
		return NULL;

	rptr += page_offset * PAGE_SIZE;
	return rptr;
}

void qxl_bo_kunmap(struct qxl_bo *bo)
{
	if (bo->kptr == NULL)
		return;
	bo->map_count--;
	if (bo->map_count > 0)
		return;
	bo->kptr = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

void qxl_bo_kunmap_atomic_page(struct qxl_device *qdev,
			       struct qxl_bo *bo, void *pmap)
{
	struct ttm_mem_type_manager *man = &bo->tbo.bdev->man[bo->tbo.mem.mem_type];

	if ((bo->tbo.mem.mem_type != TTM_PL_VRAM) &&
	    (bo->tbo.mem.mem_type != TTM_PL_PRIV))
		goto fallback;

	io_mapping_unmap_atomic(pmap);

	(void) ttm_mem_io_lock(man, false);
	ttm_mem_io_free(bo->tbo.bdev, &bo->tbo.mem);
	ttm_mem_io_unlock(man);
	return;
 fallback:
	qxl_bo_kunmap(bo);
}

void qxl_bo_unref(struct qxl_bo **bo)
{
	if ((*bo) == NULL)
		return;

	drm_gem_object_put_unlocked(&(*bo)->gem_base);
	*bo = NULL;
}

struct qxl_bo *qxl_bo_ref(struct qxl_bo *bo)
{
	drm_gem_object_get(&bo->gem_base);
	return bo;
}

static int __qxl_bo_pin(struct qxl_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct drm_device *ddev = bo->gem_base.dev;
	int r;

	if (bo->pin_count) {
		bo->pin_count++;
		return 0;
	}
	qxl_ttm_placement_from_domain(bo, bo->type, true);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (likely(r == 0)) {
		bo->pin_count = 1;
	}
	if (unlikely(r != 0))
		dev_err(ddev->dev, "%p pin failed\n", bo);
	return r;
}

static int __qxl_bo_unpin(struct qxl_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct drm_device *ddev = bo->gem_base.dev;
	int r, i;

	if (!bo->pin_count) {
		dev_warn(ddev->dev, "%p unpin not necessary\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (unlikely(r != 0))
		dev_err(ddev->dev, "%p validate failed for unpin\n", bo);
	return r;
}

/*
 * Reserve the BO before pinning the object.  If the BO was reserved
 * beforehand, use the internal version directly __qxl_bo_pin.
 *
 */
int qxl_bo_pin(struct qxl_bo *bo)
{
	int r;

	r = qxl_bo_reserve(bo, false);
	if (r)
		return r;

	r = __qxl_bo_pin(bo);
	qxl_bo_unreserve(bo);
	return r;
}

/*
 * Reserve the BO before pinning the object.  If the BO was reserved
 * beforehand, use the internal version directly __qxl_bo_unpin.
 *
 */
int qxl_bo_unpin(struct qxl_bo *bo)
{
	int r;

	r = qxl_bo_reserve(bo, false);
	if (r)
		return r;

	r = __qxl_bo_unpin(bo);
	qxl_bo_unreserve(bo);
	return r;
}

void qxl_bo_force_delete(struct qxl_device *qdev)
{
	struct qxl_bo *bo, *n;

	if (list_empty(&qdev->gem.objects))
		return;
	dev_err(qdev->ddev.dev, "Userspace still has active objects !\n");
	list_for_each_entry_safe(bo, n, &qdev->gem.objects, list) {
		dev_err(qdev->ddev.dev, "%p %p %lu %lu force free\n",
			&bo->gem_base, bo, (unsigned long)bo->gem_base.size,
			*((unsigned long *)&bo->gem_base.refcount));
		mutex_lock(&qdev->gem.mutex);
		list_del_init(&bo->list);
		mutex_unlock(&qdev->gem.mutex);
		/* this should unref the ttm bo */
		drm_gem_object_put_unlocked(&bo->gem_base);
	}
}

int qxl_bo_init(struct qxl_device *qdev)
{
	return qxl_ttm_init(qdev);
}

void qxl_bo_fini(struct qxl_device *qdev)
{
	qxl_ttm_fini(qdev);
}

int qxl_bo_check_id(struct qxl_device *qdev, struct qxl_bo *bo)
{
	int ret;

	if (bo->type == QXL_GEM_DOMAIN_SURFACE && bo->surface_id == 0) {
		/* allocate a surface id for this surface now */
		ret = qxl_surface_id_alloc(qdev, bo);
		if (ret)
			return ret;

		ret = qxl_hw_surface_alloc(qdev, bo);
		if (ret)
			return ret;
	}
	return 0;
}

int qxl_surf_evict(struct qxl_device *qdev)
{
	return ttm_bo_evict_mm(&qdev->mman.bdev, TTM_PL_PRIV);
}

int qxl_vram_evict(struct qxl_device *qdev)
{
	return ttm_bo_evict_mm(&qdev->mman.bdev, TTM_PL_VRAM);
}
