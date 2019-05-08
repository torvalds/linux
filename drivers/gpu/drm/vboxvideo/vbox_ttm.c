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
	.evict_flags = drm_gem_vram_bo_driver_evict_flags,
	.verify_access = drm_gem_vram_bo_driver_verify_access,
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
				 true);
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

int vbox_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct vbox_private *vbox = file_priv->minor->dev->dev_private;

	return ttm_bo_mmap(filp, vma, &vbox->ttm.bdev);
}
