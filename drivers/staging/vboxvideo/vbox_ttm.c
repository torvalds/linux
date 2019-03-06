// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_ttm.c
 * Copyright 2012 Red Hat Inc.
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com>
 */
#include <linux/pci.h>
#include <drm/drm_file.h>
#include <drm/ttm/ttm_page_alloc.h>
#include "vbox_drv.h"

static inline struct vbox_private *vbox_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct vbox_private, ttm.bdev);
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
		mem->bus.base = pci_resource_start(vbox->ddev.pdev, 0);
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

static void vbox_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func vbox_tt_backend_func = {
	.destroy = &vbox_ttm_backend_destroy,
};

static struct ttm_tt *vbox_ttm_tt_create(struct ttm_buffer_object *bo,
					 u32 page_flags)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	tt->func = &vbox_tt_backend_func;
	if (ttm_tt_init(tt, bo, page_flags)) {
		kfree(tt);
		return NULL;
	}

	return tt;
}

static struct ttm_bo_driver vbox_bo_driver = {
	.ttm_tt_create = vbox_ttm_tt_create,
	.init_mem_type = vbox_bo_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = vbox_bo_evict_flags,
	.verify_access = vbox_bo_verify_access,
	.io_mem_reserve = &vbox_ttm_io_mem_reserve,
	.io_mem_free = &vbox_ttm_io_mem_free,
};

int vbox_mm_init(struct vbox_private *vbox)
{
	int ret;
	struct drm_device *dev = &vbox->ddev;
	struct ttm_bo_device *bdev = &vbox->ttm.bdev;

	ret = ttm_bo_device_init(&vbox->ttm.bdev,
				 &vbox_bo_driver,
				 dev->anon_inode->i_mapping,
				 DRM_FILE_PAGE_OFFSET, true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		return ret;
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
	return ret;
}

void vbox_mm_fini(struct vbox_private *vbox)
{
#ifdef DRM_MTRR_WC
	drm_mtrr_del(vbox->fb_mtrr,
		     pci_resource_start(vbox->ddev.pdev, 0),
		     pci_resource_len(vbox->ddev.pdev, 0), DRM_MTRR_WC);
#else
	arch_phys_wc_del(vbox->fb_mtrr);
#endif
	ttm_bo_device_release(&vbox->ttm.bdev);
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

int vbox_bo_create(struct vbox_private *vbox, int size, int align,
		   u32 flags, struct vbox_bo **pvboxbo)
{
	struct vbox_bo *vboxbo;
	size_t acc_size;
	int ret;

	vboxbo = kzalloc(sizeof(*vboxbo), GFP_KERNEL);
	if (!vboxbo)
		return -ENOMEM;

	ret = drm_gem_object_init(&vbox->ddev, &vboxbo->gem, size);
	if (ret)
		goto err_free_vboxbo;

	vboxbo->bo.bdev = &vbox->ttm.bdev;

	vbox_ttm_placement(vboxbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	acc_size = ttm_bo_dma_acc_size(&vbox->ttm.bdev, size,
				       sizeof(struct vbox_bo));

	ret = ttm_bo_init(&vbox->ttm.bdev, &vboxbo->bo, size,
			  ttm_bo_type_device, &vboxbo->placement,
			  align >> PAGE_SHIFT, false, acc_size,
			  NULL, NULL, vbox_bo_ttm_destroy);
	if (ret)
		goto err_free_vboxbo;

	*pvboxbo = vboxbo;

	return 0;

err_free_vboxbo:
	kfree(vboxbo);
	return ret;
}

int vbox_bo_pin(struct vbox_bo *bo, u32 pl_flag)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (bo->pin_count) {
		bo->pin_count++;
		return 0;
	}

	ret = vbox_bo_reserve(bo, false);
	if (ret)
		return ret;

	vbox_ttm_placement(bo, pl_flag);

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
	if (ret == 0)
		bo->pin_count = 1;

	vbox_bo_unreserve(bo);

	return ret;
}

int vbox_bo_unpin(struct vbox_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	ret = vbox_bo_reserve(bo, false);
	if (ret) {
		DRM_ERROR("Error %d reserving bo, leaving it pinned\n", ret);
		return ret;
	}

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);

	vbox_bo_unreserve(bo);

	return ret;
}

/*
 * Move a vbox-owned buffer object to system memory if no one else has it
 * pinned.  The caller must have pinned it previously, and this call will
 * release the caller's pin.
 */
int vbox_bo_push_sysram(struct vbox_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	if (bo->kmap.virtual) {
		ttm_bo_kunmap(&bo->kmap);
		bo->kmap.virtual = NULL;
	}

	vbox_ttm_placement(bo, TTM_PL_FLAG_SYSTEM);

	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
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

void *vbox_bo_kmap(struct vbox_bo *bo)
{
	int ret;

	if (bo->kmap.virtual)
		return bo->kmap.virtual;

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
	if (ret) {
		DRM_ERROR("Error kmapping bo: %d\n", ret);
		return NULL;
	}

	return bo->kmap.virtual;
}

void vbox_bo_kunmap(struct vbox_bo *bo)
{
	if (bo->kmap.virtual) {
		ttm_bo_kunmap(&bo->kmap);
		bo->kmap.virtual = NULL;
	}
}
