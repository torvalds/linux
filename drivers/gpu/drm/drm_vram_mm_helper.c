// SPDX-License-Identifier: GPL-2.0-or-later

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_vram_mm_helper.h>

#include <drm/ttm/ttm_page_alloc.h>

/**
 * DOC: overview
 *
 * The data structure &struct drm_vram_mm and its helpers implement a memory
 * manager for simple framebuffer devices with dedicated video memory. Buffer
 * objects are either placed in video RAM or evicted to system memory. These
 * helper functions work well with &struct drm_gem_vram_object.
 */

/*
 * TTM TT
 */

static void backend_func_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func backend_func = {
	.destroy = backend_func_destroy
};

/*
 * TTM BO device
 */

static struct ttm_tt *bo_driver_ttm_tt_create(struct ttm_buffer_object *bo,
					      uint32_t page_flags)
{
	struct ttm_tt *tt;
	int ret;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	tt->func = &backend_func;

	ret = ttm_tt_init(tt, bo, page_flags);
	if (ret < 0)
		goto err_ttm_tt_init;

	return tt;

err_ttm_tt_init:
	kfree(tt);
	return NULL;
}

static int bo_driver_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
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
		return -EINVAL;
	}
	return 0;
}

static void bo_driver_evict_flags(struct ttm_buffer_object *bo,
				  struct ttm_placement *placement)
{
	struct drm_vram_mm *vmm = drm_vram_mm_of_bdev(bo->bdev);

	if (vmm->funcs && vmm->funcs->evict_flags)
		vmm->funcs->evict_flags(bo, placement);
}

static int bo_driver_verify_access(struct ttm_buffer_object *bo,
				   struct file *filp)
{
	struct drm_vram_mm *vmm = drm_vram_mm_of_bdev(bo->bdev);

	if (!vmm->funcs || !vmm->funcs->verify_access)
		return 0;
	return vmm->funcs->verify_access(bo, filp);
}

static int bo_driver_io_mem_reserve(struct ttm_bo_device *bdev,
				    struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = bdev->man + mem->mem_type;
	struct drm_vram_mm *vmm = drm_vram_mm_of_bdev(bdev);

	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;

	mem->bus.addr = NULL;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:	/* nothing to do */
		mem->bus.offset = 0;
		mem->bus.base = 0;
		mem->bus.is_iomem = false;
		break;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = vmm->vram_base;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void bo_driver_io_mem_free(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{ }

static struct ttm_bo_driver bo_driver = {
	.ttm_tt_create = bo_driver_ttm_tt_create,
	.ttm_tt_populate = ttm_pool_populate,
	.ttm_tt_unpopulate = ttm_pool_unpopulate,
	.init_mem_type = bo_driver_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = bo_driver_evict_flags,
	.verify_access = bo_driver_verify_access,
	.io_mem_reserve = bo_driver_io_mem_reserve,
	.io_mem_free = bo_driver_io_mem_free,
};

/*
 * struct drm_vram_mm
 */

/**
 * drm_vram_mm_init() - Initialize an instance of VRAM MM.
 * @vmm:	the VRAM MM instance to initialize
 * @dev:	the DRM device
 * @vram_base:	the base address of the video memory
 * @vram_size:	the size of the video memory in bytes
 * @funcs:	callback functions for buffer objects
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_vram_mm_init(struct drm_vram_mm *vmm, struct drm_device *dev,
		     uint64_t vram_base, size_t vram_size,
		     const struct drm_vram_mm_funcs *funcs)
{
	int ret;

	vmm->vram_base = vram_base;
	vmm->vram_size = vram_size;
	vmm->funcs = funcs;

	ret = ttm_bo_device_init(&vmm->bdev, &bo_driver,
				 dev->anon_inode->i_mapping,
				 true);
	if (ret)
		return ret;

	ret = ttm_bo_init_mm(&vmm->bdev, TTM_PL_VRAM, vram_size >> PAGE_SHIFT);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_vram_mm_init);

/**
 * drm_vram_mm_cleanup() - Cleans up an initialized instance of VRAM MM.
 * @vmm:	the VRAM MM instance to clean up
 */
void drm_vram_mm_cleanup(struct drm_vram_mm *vmm)
{
	ttm_bo_device_release(&vmm->bdev);
}
EXPORT_SYMBOL(drm_vram_mm_cleanup);

/**
 * drm_vram_mm_mmap() - Helper for implementing &struct file_operations.mmap()
 * @filp:	the mapping's file structure
 * @vma:	the mapping's memory area
 * @vmm:	the VRAM MM instance
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_vram_mm_mmap(struct file *filp, struct vm_area_struct *vma,
		     struct drm_vram_mm *vmm)
{
	return ttm_bo_mmap(filp, vma, &vmm->bdev);
}
EXPORT_SYMBOL(drm_vram_mm_mmap);

/*
 * Helpers for integration with struct drm_device
 */

/**
 * drm_vram_helper_alloc_mm - Allocates a device's instance of \
	&struct drm_vram_mm
 * @dev:	the DRM device
 * @vram_base:	the base address of the video memory
 * @vram_size:	the size of the video memory in bytes
 * @funcs:	callback functions for buffer objects
 *
 * Returns:
 * The new instance of &struct drm_vram_mm on success, or
 * an ERR_PTR()-encoded errno code otherwise.
 */
struct drm_vram_mm *drm_vram_helper_alloc_mm(
	struct drm_device *dev, uint64_t vram_base, size_t vram_size,
	const struct drm_vram_mm_funcs *funcs)
{
	int ret;

	if (WARN_ON(dev->vram_mm))
		return dev->vram_mm;

	dev->vram_mm = kzalloc(sizeof(*dev->vram_mm), GFP_KERNEL);
	if (!dev->vram_mm)
		return ERR_PTR(-ENOMEM);

	ret = drm_vram_mm_init(dev->vram_mm, dev, vram_base, vram_size, funcs);
	if (ret)
		goto err_kfree;

	return dev->vram_mm;

err_kfree:
	kfree(dev->vram_mm);
	dev->vram_mm = NULL;
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(drm_vram_helper_alloc_mm);

/**
 * drm_vram_helper_release_mm - Releases a device's instance of \
	&struct drm_vram_mm
 * @dev:	the DRM device
 */
void drm_vram_helper_release_mm(struct drm_device *dev)
{
	if (!dev->vram_mm)
		return;

	drm_vram_mm_cleanup(dev->vram_mm);
	kfree(dev->vram_mm);
	dev->vram_mm = NULL;
}
EXPORT_SYMBOL(drm_vram_helper_release_mm);

/*
 * Helpers for &struct file_operations
 */

/**
 * drm_vram_mm_file_operations_mmap() - \
	Implements &struct file_operations.mmap()
 * @filp:	the mapping's file structure
 * @vma:	the mapping's memory area
 *
 * Returns:
 * 0 on success, or
 * a negative error code otherwise.
 */
int drm_vram_mm_file_operations_mmap(
	struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev = file_priv->minor->dev;

	if (WARN_ONCE(!dev->vram_mm, "VRAM MM not initialized"))
		return -EINVAL;

	return drm_vram_mm_mmap(filp, vma, dev->vram_mm);
}
EXPORT_SYMBOL(drm_vram_mm_file_operations_mmap);
