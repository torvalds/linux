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
#include "ast_drv.h"
#include <ttm/ttm_page_alloc.h>

static inline struct ast_private *
ast_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct ast_private, ttm.bdev);
}

static int
ast_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void
ast_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int ast_ttm_global_init(struct ast_private *ast)
{
	struct drm_global_reference *global_ref;
	int r;

	global_ref = &ast->ttm.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &ast_ttm_mem_global_init;
	global_ref->release = &ast_ttm_mem_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	ast->ttm.bo_global_ref.mem_glob =
		ast->ttm.mem_global_ref.object;
	global_ref = &ast->ttm.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&ast->ttm.mem_global_ref);
		return r;
	}
	return 0;
}

static void
ast_ttm_global_release(struct ast_private *ast)
{
	if (ast->ttm.mem_global_ref.release == NULL)
		return;

	drm_global_item_unref(&ast->ttm.bo_global_ref.ref);
	drm_global_item_unref(&ast->ttm.mem_global_ref);
	ast->ttm.mem_global_ref.release = NULL;
}


static void ast_bo_ttm_destroy(struct ttm_buffer_object *tbo)
{
	struct ast_bo *bo;

	bo = container_of(tbo, struct ast_bo, bo);

	drm_gem_object_release(&bo->gem);
	kfree(bo);
}

static bool ast_ttm_bo_is_ast_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &ast_bo_ttm_destroy)
		return true;
	return false;
}

static int
ast_bo_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
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
ast_bo_evict_flags(struct ttm_buffer_object *bo, struct ttm_placement *pl)
{
	struct ast_bo *astbo = ast_bo(bo);

	if (!ast_ttm_bo_is_ast_bo(bo))
		return;

	ast_ttm_placement(astbo, TTM_PL_FLAG_SYSTEM);
	*pl = astbo->placement;
}

static int ast_bo_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	struct ast_bo *astbo = ast_bo(bo);

	return drm_vma_node_verify_access(&astbo->gem.vma_node, filp);
}

static int ast_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct ast_private *ast = ast_bdev(bdev);

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
		mem->bus.base = pci_resource_start(ast->dev->pdev, 0);
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static void ast_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int ast_bo_move(struct ttm_buffer_object *bo,
		       bool evict, bool interruptible,
		       bool no_wait_gpu,
		       struct ttm_mem_reg *new_mem)
{
	int r;
	r = ttm_bo_move_memcpy(bo, evict, no_wait_gpu, new_mem);
	return r;
}


static void ast_ttm_backend_destroy(struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_backend_func ast_tt_backend_func = {
	.destroy = &ast_ttm_backend_destroy,
};


static struct ttm_tt *ast_ttm_tt_create(struct ttm_bo_device *bdev,
				 unsigned long size, uint32_t page_flags,
				 struct page *dummy_read_page)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(struct ttm_tt), GFP_KERNEL);
	if (tt == NULL)
		return NULL;
	tt->func = &ast_tt_backend_func;
	if (ttm_tt_init(tt, bdev, size, page_flags, dummy_read_page)) {
		kfree(tt);
		return NULL;
	}
	return tt;
}

static int ast_ttm_tt_populate(struct ttm_tt *ttm)
{
	return ttm_pool_populate(ttm);
}

static void ast_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

struct ttm_bo_driver ast_bo_driver = {
	.ttm_tt_create = ast_ttm_tt_create,
	.ttm_tt_populate = ast_ttm_tt_populate,
	.ttm_tt_unpopulate = ast_ttm_tt_unpopulate,
	.init_mem_type = ast_bo_init_mem_type,
	.evict_flags = ast_bo_evict_flags,
	.move = ast_bo_move,
	.verify_access = ast_bo_verify_access,
	.io_mem_reserve = &ast_ttm_io_mem_reserve,
	.io_mem_free = &ast_ttm_io_mem_free,
};

int ast_mm_init(struct ast_private *ast)
{
	int ret;
	struct drm_device *dev = ast->dev;
	struct ttm_bo_device *bdev = &ast->ttm.bdev;

	ret = ast_ttm_global_init(ast);
	if (ret)
		return ret;

	ret = ttm_bo_device_init(&ast->ttm.bdev,
				 ast->ttm.bo_global_ref.ref.object,
				 &ast_bo_driver, DRM_FILE_PAGE_OFFSET,
				 true);
	if (ret) {
		DRM_ERROR("Error initialising bo driver; %d\n", ret);
		return ret;
	}

	ret = ttm_bo_init_mm(bdev, TTM_PL_VRAM,
			     ast->vram_size >> PAGE_SHIFT);
	if (ret) {
		DRM_ERROR("Failed ttm VRAM init: %d\n", ret);
		return ret;
	}

	ast->fb_mtrr = arch_phys_wc_add(pci_resource_start(dev->pdev, 0),
					pci_resource_len(dev->pdev, 0));

	return 0;
}

void ast_mm_fini(struct ast_private *ast)
{
	ttm_bo_device_release(&ast->ttm.bdev);

	ast_ttm_global_release(ast);

	arch_phys_wc_del(ast->fb_mtrr);
}

void ast_ttm_placement(struct ast_bo *bo, int domain)
{
	u32 c = 0;
	bo->placement.fpfn = 0;
	bo->placement.lpfn = 0;
	bo->placement.placement = bo->placements;
	bo->placement.busy_placement = bo->placements;
	if (domain & TTM_PL_FLAG_VRAM)
		bo->placements[c++] = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_VRAM;
	if (domain & TTM_PL_FLAG_SYSTEM)
		bo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	if (!c)
		bo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	bo->placement.num_placement = c;
	bo->placement.num_busy_placement = c;
}

int ast_bo_create(struct drm_device *dev, int size, int align,
		  uint32_t flags, struct ast_bo **pastbo)
{
	struct ast_private *ast = dev->dev_private;
	struct ast_bo *astbo;
	size_t acc_size;
	int ret;

	astbo = kzalloc(sizeof(struct ast_bo), GFP_KERNEL);
	if (!astbo)
		return -ENOMEM;

	ret = drm_gem_object_init(dev, &astbo->gem, size);
	if (ret) {
		kfree(astbo);
		return ret;
	}

	astbo->bo.bdev = &ast->ttm.bdev;
	astbo->bo.bdev->dev_mapping = dev->dev_mapping;

	ast_ttm_placement(astbo, TTM_PL_FLAG_VRAM | TTM_PL_FLAG_SYSTEM);

	acc_size = ttm_bo_dma_acc_size(&ast->ttm.bdev, size,
				       sizeof(struct ast_bo));

	ret = ttm_bo_init(&ast->ttm.bdev, &astbo->bo, size,
			  ttm_bo_type_device, &astbo->placement,
			  align >> PAGE_SHIFT, false, NULL, acc_size,
			  NULL, ast_bo_ttm_destroy);
	if (ret)
		return ret;

	*pastbo = astbo;
	return 0;
}

static inline u64 ast_bo_gpu_offset(struct ast_bo *bo)
{
	return bo->bo.offset;
}

int ast_bo_pin(struct ast_bo *bo, u32 pl_flag, u64 *gpu_addr)
{
	int i, ret;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = ast_bo_gpu_offset(bo);
	}

	ast_ttm_placement(bo, pl_flag);
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i] |= TTM_PL_FLAG_NO_EVICT;
	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret)
		return ret;

	bo->pin_count = 1;
	if (gpu_addr)
		*gpu_addr = ast_bo_gpu_offset(bo);
	return 0;
}

int ast_bo_unpin(struct ast_bo *bo)
{
	int i, ret;
	if (!bo->pin_count) {
		DRM_ERROR("unpin bad %p\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;

	for (i = 0; i < bo->placement.num_placement ; i++)
		bo->placements[i] &= ~TTM_PL_FLAG_NO_EVICT;
	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret)
		return ret;

	return 0;
}

int ast_bo_push_sysram(struct ast_bo *bo)
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

	ast_ttm_placement(bo, TTM_PL_FLAG_SYSTEM);
	for (i = 0; i < bo->placement.num_placement ; i++)
		bo->placements[i] |= TTM_PL_FLAG_NO_EVICT;

	ret = ttm_bo_validate(&bo->bo, &bo->placement, false, false);
	if (ret) {
		DRM_ERROR("pushing to VRAM failed\n");
		return ret;
	}
	return 0;
}

int ast_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct ast_private *ast;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET))
		return drm_mmap(filp, vma);

	file_priv = filp->private_data;
	ast = file_priv->minor->dev->dev_private;
	return ttm_bo_mmap(filp, vma, &ast->ttm.bdev);
}
