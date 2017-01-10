/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie
 *    Alon Levy
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
 */

#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_page_alloc.h>
#include <ttm/ttm_module.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/virtgpu_drm.h>
#include "virtgpu_drv.h"

#include <linux/delay.h>

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static struct
virtio_gpu_device *virtio_gpu_get_vgdev(struct ttm_bo_device *bdev)
{
	struct virtio_gpu_mman *mman;
	struct virtio_gpu_device *vgdev;

	mman = container_of(bdev, struct virtio_gpu_mman, bdev);
	vgdev = container_of(mman, struct virtio_gpu_device, mman);
	return vgdev;
}

static int virtio_gpu_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void virtio_gpu_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int virtio_gpu_ttm_global_init(struct virtio_gpu_device *vgdev)
{
	struct drm_global_reference *global_ref;
	int r;

	vgdev->mman.mem_global_referenced = false;
	global_ref = &vgdev->mman.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &virtio_gpu_ttm_mem_global_init;
	global_ref->release = &virtio_gpu_ttm_mem_global_release;

	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	vgdev->mman.bo_global_ref.mem_glob =
		vgdev->mman.mem_global_ref.object;
	global_ref = &vgdev->mman.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&vgdev->mman.mem_global_ref);
		return r;
	}

	vgdev->mman.mem_global_referenced = true;
	return 0;
}

static void virtio_gpu_ttm_global_fini(struct virtio_gpu_device *vgdev)
{
	if (vgdev->mman.mem_global_referenced) {
		drm_global_item_unref(&vgdev->mman.bo_global_ref.ref);
		drm_global_item_unref(&vgdev->mman.mem_global_ref);
		vgdev->mman.mem_global_referenced = false;
	}
}

#if 0
/*
 * Hmm, seems to not do anything useful.  Leftover debug hack?
 * Something like printing pagefaults to kernel log?
 */
static struct vm_operations_struct virtio_gpu_ttm_vm_ops;
static const struct vm_operations_struct *ttm_vm_ops;

static int virtio_gpu_ttm_fault(struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo;
	struct virtio_gpu_device *vgdev;
	int r;

	bo = (struct ttm_buffer_object *)vma->vm_private_data;
	if (bo == NULL)
		return VM_FAULT_NOPAGE;
	vgdev = virtio_gpu_get_vgdev(bo->bdev);
	r = ttm_vm_ops->fault(vma, vmf);
	return r;
}
#endif

int virtio_gpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct virtio_gpu_device *vgdev;
	int r;

	file_priv = filp->private_data;
	vgdev = file_priv->minor->dev->dev_private;
	if (vgdev == NULL) {
		DRM_ERROR(
		 "filp->private_data->minor->dev->dev_private == NULL\n");
		return -EINVAL;
	}
	r = ttm_bo_mmap(filp, vma, &vgdev->mman.bdev);
#if 0
	if (unlikely(r != 0))
		return r;
	if (unlikely(ttm_vm_ops == NULL)) {
		ttm_vm_ops = vma->vm_ops;
		virtio_gpu_ttm_vm_ops = *ttm_vm_ops;
		virtio_gpu_ttm_vm_ops.fault = &virtio_gpu_ttm_fault;
	}
	vma->vm_ops = &virtio_gpu_ttm_vm_ops;
	return 0;
#else
	return r;
#endif
}

static int virtio_gpu_invalidate_caches(struct ttm_bo_device *bdev,
					uint32_t flags)
{
	return 0;
}

static int ttm_bo_man_get_node(struct ttm_mem_type_manager *man,
			       struct ttm_buffer_object *bo,
			       const struct ttm_place *place,
			       struct ttm_mem_reg *mem)
{
	mem->mm_node = (void *)1;
	return 0;
}

static void ttm_bo_man_put_node(struct ttm_mem_type_manager *man,
				struct ttm_mem_reg *mem)
{
	mem->mm_node = (void *)NULL;
	return;
}

static int ttm_bo_man_init(struct ttm_mem_type_manager *man,
			   unsigned long p_size)
{
	return 0;
}

static int ttm_bo_man_takedown(struct ttm_mem_type_manager *man)
{
	return 0;
}

static void ttm_bo_man_debug(struct ttm_mem_type_manager *man,
			     const char *prefix)
{
}

static const struct ttm_mem_type_manager_func virtio_gpu_bo_manager_func = {
	ttm_bo_man_init,
	ttm_bo_man_takedown,
	ttm_bo_man_get_node,
	ttm_bo_man_put_node,
	ttm_bo_man_debug
};

static int virtio_gpu_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
				    struct ttm_mem_type_manager *man)
{
	struct virtio_gpu_device *vgdev;

	vgdev = virtio_gpu_get_vgdev(bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_TT:
		man->func = &virtio_gpu_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void virtio_gpu_evict_flags(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	static struct ttm_place placements = {
		.fpfn  = 0,
		.lpfn  = 0,
		.flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM,
	};

	placement->placement = &placements;
	placement->busy_placement = &placements;
	placement->num_placement = 1;
	placement->num_busy_placement = 1;
	return;
}

static int virtio_gpu_verify_access(struct ttm_buffer_object *bo,
				    struct file *filp)
{
	return 0;
}

static int virtio_gpu_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
					 struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
	case TTM_PL_TT:
		/* system memory */
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

static void virtio_gpu_ttm_io_mem_free(struct ttm_bo_device *bdev,
				       struct ttm_mem_reg *mem)
{
}

/*
 * TTM backend functions.
 */
struct virtio_gpu_ttm_tt {
	struct ttm_dma_tt		ttm;
	struct virtio_gpu_device	*vgdev;
	u64				offset;
};

static int virtio_gpu_ttm_backend_bind(struct ttm_tt *ttm,
				       struct ttm_mem_reg *bo_mem)
{
	struct virtio_gpu_ttm_tt *gtt = (void *)ttm;

	gtt->offset = (unsigned long)(bo_mem->start << PAGE_SHIFT);
	if (!ttm->num_pages)
		WARN(1, "nothing to bind %lu pages for mreg %p back %p!\n",
		     ttm->num_pages, bo_mem, ttm);

	/* Not implemented */
	return 0;
}

static int virtio_gpu_ttm_backend_unbind(struct ttm_tt *ttm)
{
	/* Not implemented */
	return 0;
}

static void virtio_gpu_ttm_backend_destroy(struct ttm_tt *ttm)
{
	struct virtio_gpu_ttm_tt *gtt = (void *)ttm;

	ttm_dma_tt_fini(&gtt->ttm);
	kfree(gtt);
}

static struct ttm_backend_func virtio_gpu_backend_func = {
	.bind = &virtio_gpu_ttm_backend_bind,
	.unbind = &virtio_gpu_ttm_backend_unbind,
	.destroy = &virtio_gpu_ttm_backend_destroy,
};

static int virtio_gpu_ttm_tt_populate(struct ttm_tt *ttm)
{
	if (ttm->state != tt_unpopulated)
		return 0;

	return ttm_pool_populate(ttm);
}

static void virtio_gpu_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

static struct ttm_tt *virtio_gpu_ttm_tt_create(struct ttm_bo_device *bdev,
					       unsigned long size,
					       uint32_t page_flags,
					       struct page *dummy_read_page)
{
	struct virtio_gpu_device *vgdev;
	struct virtio_gpu_ttm_tt *gtt;

	vgdev = virtio_gpu_get_vgdev(bdev);
	gtt = kzalloc(sizeof(struct virtio_gpu_ttm_tt), GFP_KERNEL);
	if (gtt == NULL)
		return NULL;
	gtt->ttm.ttm.func = &virtio_gpu_backend_func;
	gtt->vgdev = vgdev;
	if (ttm_dma_tt_init(&gtt->ttm, bdev, size, page_flags,
			    dummy_read_page)) {
		kfree(gtt);
		return NULL;
	}
	return &gtt->ttm.ttm;
}

static void virtio_gpu_move_null(struct ttm_buffer_object *bo,
				 struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	BUG_ON(old_mem->mm_node != NULL);
	*old_mem = *new_mem;
	new_mem->mm_node = NULL;
}

static int virtio_gpu_bo_move(struct ttm_buffer_object *bo,
			      bool evict, bool interruptible,
			      bool no_wait_gpu,
			      struct ttm_mem_reg *new_mem)
{
	int ret;

	ret = ttm_bo_wait(bo, interruptible, no_wait_gpu);
	if (ret)
		return ret;

	virtio_gpu_move_null(bo, new_mem);
	return 0;
}

static void virtio_gpu_bo_move_notify(struct ttm_buffer_object *tbo,
				      struct ttm_mem_reg *new_mem)
{
	struct virtio_gpu_object *bo;
	struct virtio_gpu_device *vgdev;

	bo = container_of(tbo, struct virtio_gpu_object, tbo);
	vgdev = (struct virtio_gpu_device *)bo->gem_base.dev->dev_private;

	if (!new_mem || (new_mem->placement & TTM_PL_FLAG_SYSTEM)) {
		if (bo->hw_res_handle)
			virtio_gpu_cmd_resource_inval_backing(vgdev,
							   bo->hw_res_handle);

	} else if (new_mem->placement & TTM_PL_FLAG_TT) {
		if (bo->hw_res_handle) {
			virtio_gpu_object_attach(vgdev, bo, bo->hw_res_handle,
						 NULL);
		}
	}
}

static void virtio_gpu_bo_swap_notify(struct ttm_buffer_object *tbo)
{
	struct virtio_gpu_object *bo;
	struct virtio_gpu_device *vgdev;

	bo = container_of(tbo, struct virtio_gpu_object, tbo);
	vgdev = (struct virtio_gpu_device *)bo->gem_base.dev->dev_private;

	if (bo->pages)
		virtio_gpu_object_free_sg_table(bo);
}

static struct ttm_bo_driver virtio_gpu_bo_driver = {
	.ttm_tt_create = &virtio_gpu_ttm_tt_create,
	.ttm_tt_populate = &virtio_gpu_ttm_tt_populate,
	.ttm_tt_unpopulate = &virtio_gpu_ttm_tt_unpopulate,
	.invalidate_caches = &virtio_gpu_invalidate_caches,
	.init_mem_type = &virtio_gpu_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = &virtio_gpu_evict_flags,
	.move = &virtio_gpu_bo_move,
	.verify_access = &virtio_gpu_verify_access,
	.io_mem_reserve = &virtio_gpu_ttm_io_mem_reserve,
	.io_mem_free = &virtio_gpu_ttm_io_mem_free,
	.move_notify = &virtio_gpu_bo_move_notify,
	.swap_notify = &virtio_gpu_bo_swap_notify,
	.lru_tail = &ttm_bo_default_lru_tail,
	.swap_lru_tail = &ttm_bo_default_swap_lru_tail,
};

int virtio_gpu_ttm_init(struct virtio_gpu_device *vgdev)
{
	int r;

	r = virtio_gpu_ttm_global_init(vgdev);
	if (r)
		return r;
	/* No others user of address space so set it to 0 */
	r = ttm_bo_device_init(&vgdev->mman.bdev,
			       vgdev->mman.bo_global_ref.ref.object,
			       &virtio_gpu_bo_driver,
			       vgdev->ddev->anon_inode->i_mapping,
			       DRM_FILE_PAGE_OFFSET, 0);
	if (r) {
		DRM_ERROR("failed initializing buffer object driver(%d).\n", r);
		goto err_dev_init;
	}

	r = ttm_bo_init_mm(&vgdev->mman.bdev, TTM_PL_TT, 0);
	if (r) {
		DRM_ERROR("Failed initializing GTT heap.\n");
		goto err_mm_init;
	}
	return 0;

err_mm_init:
	ttm_bo_device_release(&vgdev->mman.bdev);
err_dev_init:
	virtio_gpu_ttm_global_fini(vgdev);
	return r;
}

void virtio_gpu_ttm_fini(struct virtio_gpu_device *vgdev)
{
	ttm_bo_device_release(&vgdev->mman.bdev);
	virtio_gpu_ttm_global_fini(vgdev);
	DRM_INFO("virtio_gpu: ttm finalized\n");
}
