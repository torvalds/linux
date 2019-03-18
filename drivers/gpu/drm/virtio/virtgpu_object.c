/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/ttm/ttm_execbuf_util.h>

#include "virtgpu_drv.h"

static int virtio_gpu_resource_id_get(struct virtio_gpu_device *vgdev,
				       uint32_t *resid)
{
#if 0
	int handle = ida_alloc(&vgdev->resource_ida, GFP_KERNEL);

	if (handle < 0)
		return handle;
#else
	static int handle;

	/*
	 * FIXME: dirty hack to avoid re-using IDs, virglrenderer
	 * can't deal with that.  Needs fixing in virglrenderer, also
	 * should figure a better way to handle that in the guest.
	 */
	handle++;
#endif

	*resid = handle + 1;
	return 0;
}

static void virtio_gpu_resource_id_put(struct virtio_gpu_device *vgdev, uint32_t id)
{
#if 0
	ida_free(&vgdev->resource_ida, id - 1);
#endif
}

static void virtio_gpu_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct virtio_gpu_object *bo;
	struct virtio_gpu_device *vgdev;

	bo = container_of(tbo, struct virtio_gpu_object, tbo);
	vgdev = (struct virtio_gpu_device *)bo->gem_base.dev->dev_private;

	if (bo->created)
		virtio_gpu_cmd_unref_resource(vgdev, bo->hw_res_handle);
	if (bo->pages)
		virtio_gpu_object_free_sg_table(bo);
	drm_gem_object_release(&bo->gem_base);
	virtio_gpu_resource_id_put(vgdev, bo->hw_res_handle);
	kfree(bo);
}

static void virtio_gpu_init_ttm_placement(struct virtio_gpu_object *vgbo)
{
	u32 c = 1;

	vgbo->placement.placement = &vgbo->placement_code;
	vgbo->placement.busy_placement = &vgbo->placement_code;
	vgbo->placement_code.fpfn = 0;
	vgbo->placement_code.lpfn = 0;
	vgbo->placement_code.flags =
		TTM_PL_MASK_CACHING | TTM_PL_FLAG_TT |
		TTM_PL_FLAG_NO_EVICT;
	vgbo->placement.num_placement = c;
	vgbo->placement.num_busy_placement = c;

}

int virtio_gpu_object_create(struct virtio_gpu_device *vgdev,
			     struct virtio_gpu_object_params *params,
			     struct virtio_gpu_object **bo_ptr,
			     struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo;
	size_t acc_size;
	int ret;

	*bo_ptr = NULL;

	acc_size = ttm_bo_dma_acc_size(&vgdev->mman.bdev, params->size,
				       sizeof(struct virtio_gpu_object));

	bo = kzalloc(sizeof(struct virtio_gpu_object), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	ret = virtio_gpu_resource_id_get(vgdev, &bo->hw_res_handle);
	if (ret < 0) {
		kfree(bo);
		return ret;
	}
	params->size = roundup(params->size, PAGE_SIZE);
	ret = drm_gem_object_init(vgdev->ddev, &bo->gem_base, params->size);
	if (ret != 0) {
		virtio_gpu_resource_id_put(vgdev, bo->hw_res_handle);
		kfree(bo);
		return ret;
	}
	bo->dumb = params->dumb;

	if (params->virgl) {
		virtio_gpu_cmd_resource_create_3d(vgdev, bo, params, fence);
	} else {
		virtio_gpu_cmd_create_resource(vgdev, bo, params, fence);
	}

	virtio_gpu_init_ttm_placement(bo);
	ret = ttm_bo_init(&vgdev->mman.bdev, &bo->tbo, params->size,
			  ttm_bo_type_device, &bo->placement, 0,
			  true, acc_size, NULL, NULL,
			  &virtio_gpu_ttm_bo_destroy);
	/* ttm_bo_init failure will call the destroy */
	if (ret != 0)
		return ret;

	if (fence) {
		struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
		struct list_head validate_list;
		struct ttm_validate_buffer mainbuf;
		struct ww_acquire_ctx ticket;
		unsigned long irq_flags;
		bool signaled;

		INIT_LIST_HEAD(&validate_list);
		memset(&mainbuf, 0, sizeof(struct ttm_validate_buffer));

		/* use a gem reference since unref list undoes them */
		drm_gem_object_get(&bo->gem_base);
		mainbuf.bo = &bo->tbo;
		list_add(&mainbuf.head, &validate_list);

		ret = virtio_gpu_object_list_validate(&ticket, &validate_list);
		if (ret == 0) {
			spin_lock_irqsave(&drv->lock, irq_flags);
			signaled = virtio_fence_signaled(&fence->f);
			if (!signaled)
				/* virtio create command still in flight */
				ttm_eu_fence_buffer_objects(&ticket, &validate_list,
							    &fence->f);
			spin_unlock_irqrestore(&drv->lock, irq_flags);
			if (signaled)
				/* virtio create command finished */
				ttm_eu_backoff_reservation(&ticket, &validate_list);
		}
		virtio_gpu_unref_list(&validate_list);
	}

	*bo_ptr = bo;
	return 0;
}

void virtio_gpu_object_kunmap(struct virtio_gpu_object *bo)
{
	bo->vmap = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

int virtio_gpu_object_kmap(struct virtio_gpu_object *bo)
{
	bool is_iomem;
	int r;

	WARN_ON(bo->vmap);

	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;
	bo->vmap = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	return 0;
}

int virtio_gpu_object_get_sg_table(struct virtio_gpu_device *qdev,
				   struct virtio_gpu_object *bo)
{
	int ret;
	struct page **pages = bo->tbo.ttm->pages;
	int nr_pages = bo->tbo.num_pages;
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false
	};

	/* wtf swapping */
	if (bo->pages)
		return 0;

	if (bo->tbo.ttm->state == tt_unpopulated)
		bo->tbo.ttm->bdev->driver->ttm_tt_populate(bo->tbo.ttm, &ctx);
	bo->pages = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!bo->pages)
		goto out;

	ret = sg_alloc_table_from_pages(bo->pages, pages, nr_pages, 0,
					nr_pages << PAGE_SHIFT, GFP_KERNEL);
	if (ret)
		goto out;
	return 0;
out:
	kfree(bo->pages);
	bo->pages = NULL;
	return -ENOMEM;
}

void virtio_gpu_object_free_sg_table(struct virtio_gpu_object *bo)
{
	sg_free_table(bo->pages);
	kfree(bo->pages);
	bo->pages = NULL;
}

int virtio_gpu_object_wait(struct virtio_gpu_object *bo, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, NULL);
	if (unlikely(r != 0))
		return r;
	r = ttm_bo_wait(&bo->tbo, true, no_wait);
	ttm_bo_unreserve(&bo->tbo);
	return r;
}

