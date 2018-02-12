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

#include "cirrus_drv.h"

static inline struct cirrus_device *
cirrus_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct cirrus_device, ttm.bdev);
}

static int
cirrus_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void
cirrus_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int cirrus_ttm_global_init(struct cirrus_device *cirrus)
{
	struct drm_global_reference *global_ref;
	int r;

	global_ref = &cirrus->ttm.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &cirrus_ttm_mem_global_init;
	global_ref->release = &cirrus_ttm_mem_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	cirrus->ttm.bo_global_ref.mem_glob =
		cirrus->ttm.mem_global_ref.object;
	global_ref = &cirrus->ttm.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&cirrus->ttm.mem_global_ref);
		return r;
	}
	return 0;
}

static void
cirrus_ttm_global_release(struct cirrus_device *cirrus)
{
	if (cirrus->ttm.mem_global_ref.release == NULL)
		return;

	drm_global_item_unref(&cirrus->ttm.bo_global_ref.ref);
	drm_global_item_unref(&cirrus->ttm.mem_global_ref);
	cirrus->ttm.mem_global_ref.release = NULL;
}


static void cirrus_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
	struct cirrus_bo *bo;

	bo = container_of(tbo, struct cirrus_bo, bo);

	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static bool cirrus_ttm_bo_is_cirrus_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &cirrus_bo_ttm_destroy)
		return true;
	return false;
}

static int
cirrus_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
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

static void
cirrus_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct cirrus_bo *cirrusbo = cirrus_bo(bo);

	if (!cirrus_ttm_bo_is_cirrus_bo(bo))
		return;

	cirrus_ttm_placement(cirrusbo, TTM_PL_FLAG_SYSTEM);
	*pl = cirrusbo->placement;
}

static int cirrus_bo_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct cirrus_bo *cirrusbo = cirrus_bo(bo);

	return drm_vma_node_verify_access(&cirrusbo->gem.vma_node,
					  filp->private_data);
}

static int cirrus_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct cirrus_device *cirrus = cirrus_bdev(bdev);

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
		mem->bus.base = pci_resource_start(cirrus->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static void cirrus_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static void cirrus_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func cirrus_tt_backend_func = {
	.destroy = &cirrus_ttm_backend_destroy,
};


static struct ttm_tt *cirrus_ttm_tt_create(struct ttm_bo_device *bdev,
				 unsigned long size, uint32_t page_flags,
				 struct page *dummy_read_page)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(struct ttm_tt), GFP_KERNEL);
	if (tt == NULL)
		return NULL;
	tt->func = &cirrus_tt_backend_func;
	if (ttm_tt_init(tt, bdev, size, page_flags, dummy_read_page)) {
		kfree(tt);
		return NULL;
	}
	return tt;
}

static int cirrus_ttm_tt_populate(struct ttm_tt *ttm,
		struct ttm_operation_ctx *ctx)
{
	return ttm_pool_populate(ttm, ctx);
}

static void cirrus_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

struct ttm_bo_driver cirrus_bo_driver = {
	.ttm_tt_create = cirrus_ttm_tt_create,
	.ttm_tt_populate = cirrus_ttm_tt_populate,
	.ttm_tt_unpopulate = cirrus_ttm_tt_unpopulate,
	.init_mem_type = cirrus_bo_init_mem_type,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = cirrus_bo_evict_flags,
	.move = NULL,
	.verify_access = cirrus_bo_verify_access,
	.io_mem_reserve = &cirrus_ttm_io_mem_reserve,
	.io_mem_free = &cirrus_ttm_io_mem_free,
};

int cirrus_mm_init(struct cirrus_device *cirrus)
{
	int ret;
	struct drm_device *dev = cirrus->dev;
	struct ttm_bo_device *bdev = &cirrus->ttm.bdev;

	ret = cirrus_ttm_global_init(cirrus);
	if (ret)
		return ret;

	ret = ttm_bo_device_init(&cirrus->ttm.bdev,
				 cirrus->ttm.bo_global_ref.ref.object,
				 &cirrus_bo_driver,
				 dev->anon_inode->i_mapping,
				 DRM_FILE_PAGE_OFFSET,
				 true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		return ret;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     cirrus->mc.vram_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		return ret;
	}

	arch_io_reserve_memtype_wc(pci_resource_start(dev->pdev, 0),
				   pci_resource_len(dev->pdev, 0));

	cirrus->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
					   pci_resource_len(dev->pdev, 0));

	cirrus->mm_inited = true;
	return 0;
}

void cirrus_mm_fini(struct cirrus_device *cirrus)
{
	struct drm_device *dev = cirrus->dev;

	if (!cirrus->mm_inited)
		return;

	ttm_bo_device_release(&cirrus->ttm.bdev);

	cirrus_ttm_global_release(cirrus);

	arch_phys_wc_del(cirrus->fb_mtrr);
	cirrus->fb_mtrr = 0;
	arch_io_free_memtype_wc(pci_resource_start(dev->pdev, 0),
				pci_resource_len(dev->pdev, 0));
}

void cirrus_ttm_placement(struct cirrus_bo *bo, int domain)
{
	u32 c = 0;
	unsigned i;
	bo->placement.placement = bo->placements;
	bo->placement.busy_placement = bo->placements;
	if (domain & TTM_PL_FLAG_VRAM)
		bo->placements[c++].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_VRAM;
	if (domain & TTM_PL_FLAG_SYSTEM)
		bo->placements[c++].flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	if (!c)
		bo->placements[c++].flags = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	bo->placement.num_placement = c;
	bo->placement.num_busy_placement = c;
	for (i = 0; i < c; ++i) {
		bo->placements[i].fpfn = 0;
		bo->placements[i].lpfn = 0;
	}
}

int cirrus_bo_create(struct drm_device *dev, int size, int align,
		  uint32_t flags, struct cirrus_bo **pcirrusbo)
{
	struct cirrus_device *cirrus = dev->dev_private;
	struct cirrus_bo *cirrusbo;
	size_t acc_size;
	int ret;

	cirrusbo = kzalloc(sizeof(struct cirrus_bo), GFP_KERNEL);
	if (!cirrusbo)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &cirrusbo->gem, size);
	if (ret) {
		kfree(cirrusbo);
		return ret;
	}

	cirrusbo->bo.bdev = &cirrus->ttm.bdev;

	cirrus_ttm_placement(cirrusbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	acc_size = ttm_bo_dma_acc_size(&cirrus->ttm.bdev, size,
				       sizeof(struct cirrus_bo));

	ret = ttm_bo_init(&cirrus->ttm.bdev, &cirrusbo->bo, size,
			  ttm_bo_type_device, &cirrusbo->placement,
			  align >> PAGE_SHIFT, false, NULL, acc_size,
			  NULL, NULL, cirrus_bo_ttm_destroy);
	if (ret)
		return ret;

	*pcirrusbo = cirrusbo;
	return 0;
}

static inline u64 cirrus_bo_gpu_offset(struct cirrus_bo *bo)
{
	return bo->bo.offset;
}

int cirrus_bo_pin(struct cirrus_bo *bo, u32 pl_flag, u64 *gpu_addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = cirrus_bo_gpu_offset(bo);
	}

	cirrus_ttm_placement(bo, pl_flag);
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;
	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
	if (ret)
		return ret;

	bo->pin_count = 1;
	if (gpu_addr)
		*gpu_addr = cirrus_bo_gpu_offset(bo);
	return 0;
}

int cirrus_bo_push_sysram(struct cirrus_bo *bo)
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

	if (bo->kmap.virtual)
		ttm_bo_kunmap(&bo->kmap);

	cirrus_ttm_placement(bo, TTM_PL_FLAG_SYSTEM);
	for (i = 0; i < bo->placement.num_placement ; i++)
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, &ctx);
	if (ret) {
		DRM_ERROR("pushing to VRAM failed\n");
		return ret;
	}
	return 0;
}

int cirrus_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct cirrus_device *cirrus;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return -EINVAL;

	file_priv = filp->private_data;
	cirrus = file_priv->minor->dev->dev_private;
	return ttm_bo_mmap(filp, vma, &cirrus->ttm.bdev);
}
