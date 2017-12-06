/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_ttm.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com>
 */
#include "vbox_drv.h"
#include <ttm/ttm_page_alloc.h>

static inline struct vbox_private *vbox_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct vbox_private, ttm.bdev);
}

static int vbox_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void vbox_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

/**
 * Adds the vbox memory manager object/structures to the global memory manager.
 */
static int vbox_ttm_global_init(struct vbox_private *vbox)
{
	struct drm_global_reference *global_ref;
	int ret;

	global_ref = &vbox->ttm.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &vbox_ttm_mem_global_init;
	global_ref->release = &vbox_ttm_mem_global_release;
	ret = drm_global_item_ref(global_ref);
	if (ret) {
		DRM_ERROR("Failed setting up TTM memory subsystem.\n");
		return ret;
	}

	vbox->ttm.bo_global_ref.mem_glob = vbox->ttm.mem_global_ref.object;
	global_ref = &vbox->ttm.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;

	ret = drm_global_item_ref(global_ref);
	if (ret) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&vbox->ttm.mem_global_ref);
		return ret;
	}

	return 0;
}

/**
 * Removes the vbox memory manager object from the global memory manager.
 */
static void vbox_ttm_global_release(struct vbox_private *vbox)
{
	drm_global_item_unref(&vbox->ttm.bo_global_ref.ref);
	drm_global_item_unref(&vbox->ttm.mem_global_ref);
}

static void vbox_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
	struct vbox_bo *bo;

	bo = container_of(tbo, struct vbox_bo, bo);

	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static bool vbox_ttm_bo_is_vbox_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &vbox_bo_ttm_destroy)
		return true;

	return false;
}

static int
vbox_bo_init_mem_type(struct ttm_bo_device *bdev, u32 type,
		      struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		man->func = &ttm_bo_manager_func;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned int)type);
		return -EINVAL;
	}

	return 0;
}

static void
vbox_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct vbox_bo *vboxbo = vbox_bo(bo);

	if (!vbox_ttm_bo_is_vbox_bo(bo))
		return;

	vbox_ttm_placement(vboxbo, TTM_PL_FLAG_SYSTEM);
	*pl = vboxbo->placement;
}

static int vbox_bo_verify_access(struct ttm_buffer_object *bo,
				 struct file *filp)
{
	return 0;
}

static int vbox_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				   struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct vbox_private *vbox = vbox_bdev(bdev);

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = pci_resource_start(vbox->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void vbox_ttm_io_mem_free(struct ttm_bo_device *bdev,
				 struct ttm_mem_reg *mem)
{
}

static int vbox_bo_move(struct ttm_buffer_object *bo,
			bool evict, bool interruptible,
			bool no_wait_gpu, struct ttm_mem_reg *new_mem)
{
	return ttm_bo_move_memcpy(bo, interruptible, no_wait_gpu, new_mem);
}

static void vbox_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func vbox_tt_backend_func = {
	.destroy = &vbox_ttm_backend_destroy,
};

static struct ttm_tt *vbox_ttm_tt_create(struct ttm_bo_device *bdev,
					 unsigned long size,
					 u32 page_flags,
					 struct page *dummy_read_page)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	tt->func = &vbox_tt_backend_func;
	if (ttm_tt_init(tt, bdev, size, page_flags, dummy_read_page)) {
		kfree(tt);
		return NULL;
	}

	return tt;
}

static int vbox_ttm_tt_populate(struct ttm_tt *ttm)
{
	return ttm_pool_populate(ttm);
}

static void vbox_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

static struct ttm_bo_driver vbox_bo_driver = {
	.ttm_tt_create = vbox_ttm_tt_create,
	.ttm_tt_populate = vbox_ttm_tt_populate,
	.ttm_tt_unpopulate = vbox_ttm_tt_unpopulate,
	.init_mem_type = vbox_bo_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = vbox_bo_evict_flags,
	.move = vbox_bo_move,
	.verify_access = vbox_bo_verify_access,
	.io_mem_reserve = &vbox_ttm_io_mem_reserve,
	.io_mem_free = &vbox_ttm_io_mem_free,
	.io_mem_pfn = ttm_bo_default_io_mem_pfn,
};

int vbox_mm_init(struct vbox_private *vbox)
{
	int ret;
	struct drm_device *dev = vbox->dev;
	struct ttm_bo_device *bdev = &vbox->ttm.bdev;

	ret = vbox_ttm_global_init(vbox);
	if (ret)
		return ret;

	ret = ttm_bo_device_init(&vbox->ttm.bdev,
				 vbox->ttm.bo_global_ref.ref.object,
				 &vbox_bo_driver,
				 dev->anon_inode->i_mapping,
				 DRM_FILE_PAGE_OFFSET, true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		goto err_ttm_global_release;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     vbox->available_vram_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		goto err_device_release;
	}

#ifdef DRM_MTRR_WC
	vbox->fb_mtrr = drm_mtrr_add(pci_resource_start(dev->pdev, 0),
				     pci_resource_len(dev->pdev, 0),
				     DRM_MTRR_WC);
#else
	vbox->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
					 pci_resource_len(dev->pdev, 0));
#endif
	return 0;

err_device_release:
	ttm_bo_device_release(&vbox->ttm.bdev);
err_ttm_global_release:
	vbox_ttm_global_release(vbox);
	return ret;
}

void vbox_mm_fini(struct vbox_private *vbox)
{
#ifdef DRM_MTRR_WC
	drm_mtrr_del(vbox->fb_mtrr,
		     pci_resource_start(vbox->dev->pdev, 0),
		     pci_resource_len(vbox->dev->pdev, 0), DRM_MTRR_WC);
#else
	arch_phys_wc_del(vbox->fb_mtrr);
#endif
	ttm_bo_device_release(&vbox->ttm.bdev);
	vbox_ttm_global_release(vbox);
}

void vbox_ttm_placement(struct vbox_bo *bo, int domain)
{
	unsigned int i;
	u32 c = 0;

	bo->placement.placement = bo->placements;
	bo->placement.busy_placement = bo->placements;

	if (domain & TTM_PL_FLAG_VRAM)
		bo->placements[c++].flags =
		    TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_VRAM;
	if (domain & TTM_PL_FLAG_SYSTEM)
		bo->placements[c++].flags =
		    TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	if (!c)
		bo->placements[c++].flags =
		    TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;

	bo->placement.num_placement = c;
	bo->placement.num_busy_placement = c;

	for (i = 0; i < c; ++i) {
		bo->placements[i].fpfn = 0;
		bo->placements[i].lpfn = 0;
	}
}

int vbox_bo_create(struct drm_device *dev, int size, int align,
		   u32 flags, struct vbox_bo **pvboxbo)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_bo *vboxbo;
	size_t acc_size;
	int ret;

	vboxbo = kzalloc(sizeof(*vboxbo), GFP_KERNEL);
	if (!vboxbo)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &vboxbo->gem, size);
	if (ret)
		goto err_free_vboxbo;

	vboxbo->bo.bdev = &vbox->ttm.bdev;

	vbox_ttm_placement(vboxbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	acc_size = ttm_bo_dma_acc_size(&vbox->ttm.bdev, size,
				       sizeof(struct vbox_bo));

	ret = ttm_bo_init(&vbox->ttm.bdev, &vboxbo->bo, size,
			  ttm_bo_type_device, &vboxbo->placement,
			  align >> PAGE_SHIFT, false, NULL, acc_size,
			  NULL, NULL, vbox_bo_ttm_destroy);
	if (ret)
		goto err_free_vboxbo;

	*pvboxbo = vboxbo;

	return 0;

err_free_vboxbo:
	kfree(vboxbo);
	return ret;
}

static inline u64 vbox_bo_gpu_offset(struct vbox_bo *bo)
{
	return bo->bo.offset;
}

int vbox_bo_pin(struct vbox_bo *bo, u32 pl_flag, u64 *gpu_addr)
{
	int i, ret;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = vbox_bo_gpu_offset(bo);

		return 0;
	}

	vbox_ttm_placement(bo, pl_flag);

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret)
		return ret;

	bo->pin_count = 1;

	if (gpu_addr)
		*gpu_addr = vbox_bo_gpu_offset(bo);

	return 0;
}

int vbox_bo_unpin(struct vbox_bo *bo)
{
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret)
		return ret;

	return 0;
}

/*
 * Move a vbox-owned buffer object to system memory if no one else has it
 * pinned.  The caller must have pinned it previously, and this call will
 * release the caller's pin.
 */
int vbox_bo_push_sysram(struct vbox_bo *bo)
{
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	if (bo->kmap.virtual)
		ttm_bo_kunmap(&bo->kmap);

	vbox_ttm_placement(bo, TTM_PL_FLAG_SYSTEM);

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret) {
		DRM_ERROR("pushing to VRAM failed\n");
		return ret;
	}

	return 0;
}

int vbox_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct vbox_private *vbox;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	vbox = file_priv->minor->dev->dev_private;

	return ttm_bo_mmap(filp, vma, &vbox->ttm.bdev);
}
