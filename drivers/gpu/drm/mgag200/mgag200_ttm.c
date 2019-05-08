/*
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
 */
/*
 * Authors: Dave Airlie <airlied@redhat.com>
 */
#include <drm/drmP.h>
#include <drm/ttm/ttm_page_alloc.h>

#include "mgag200_drv.h"

static inline struct mga_device *
mgag200_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct mga_device, ttm.bdev);
}

static int
mgag200_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
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
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED |
			TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static int mgag200_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct mga_device *mdev = mgag200_bdev(bdev);

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
		mem->bus.base = pci_resource_start(mdev->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static void mgag200_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static void mgag200_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func mgag200_tt_backend_func = {
	.destroy = &mgag200_ttm_backend_destroy,
};


static struct ttm_tt *mgag200_ttm_tt_create(struct ttm_buffer_object *bo,
					    uint32_t page_flags)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(struct ttm_tt), GFP_KERNEL);
	if (tt == NULL)
		return NULL;
	tt->func = &mgag200_tt_backend_func;
	if (ttm_tt_init(tt, bo, page_flags)) {
		kfree(tt);
		return NULL;
	}
	return tt;
}

struct ttm_bo_driver mgag200_bo_driver = {
	.ttm_tt_create = mgag200_ttm_tt_create,
	.init_mem_type = mgag200_bo_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = drm_gem_vram_bo_driver_evict_flags,
	.move = NULL,
	.verify_access = drm_gem_vram_bo_driver_verify_access,
	.io_mem_reserve = &mgag200_ttm_io_mem_reserve,
	.io_mem_free = &mgag200_ttm_io_mem_free,
};

int mgag200_mm_init(struct mga_device *mdev)
{
	int ret;
	struct drm_device *dev = mdev->dev;
	struct ttm_bo_device *bdev = &mdev->ttm.bdev;

	ret = ttm_bo_device_init(&mdev->ttm.bdev,
				 &mgag200_bo_driver,
				 dev->anon_inode->i_mapping,
				 true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		return ret;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM, mdev->mc.vram_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		return ret;
	}

	arch_io_reserve_memtype_wc(pci_resource_start(dev->pdev, 0),
				   pci_resource_len(dev->pdev, 0));

	mdev->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
					 pci_resource_len(dev->pdev, 0));

	return 0;
}

void mgag200_mm_fini(struct mga_device *mdev)
{
	struct drm_device *dev = mdev->dev;

	ttm_bo_device_release(&mdev->ttm.bdev);

	arch_io_free_memtype_wc(pci_resource_start(dev->pdev, 0),
				pci_resource_len(dev->pdev, 0));
	arch_phys_wc_del(mdev->fb_mtrr);
	mdev->fb_mtrr = 0;
}

int mgag200_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct mga_device *mdev = file_priv->minor->dev->dev_private;

	return ttm_bo_mmap(filp, vma, &mdev->ttm.bdev);
}
