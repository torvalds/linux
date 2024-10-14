// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>
#include <drm/drm_prime.h>

#include "lsdc_drv.h"
#include "lsdc_ttm.h"

const char *lsdc_mem_type_to_str(uint32_t mem_type)
{
	switch (mem_type) {
	case TTM_PL_VRAM:
		return "VRAM";
	case TTM_PL_TT:
		return "GTT";
	case TTM_PL_SYSTEM:
		return "SYSTEM";
	default:
		break;
	}

	return "Unknown";
}

const char *lsdc_domain_to_str(u32 domain)
{
	switch (domain) {
	case LSDC_GEM_DOMAIN_VRAM:
		return "VRAM";
	case LSDC_GEM_DOMAIN_GTT:
		return "GTT";
	case LSDC_GEM_DOMAIN_SYSTEM:
		return "SYSTEM";
	default:
		break;
	}

	return "Unknown";
}

static void lsdc_bo_set_placement(struct lsdc_bo *lbo, u32 domain)
{
	u32 c = 0;
	u32 pflags = 0;
	u32 i;

	if (lbo->tbo.base.size <= PAGE_SIZE)
		pflags |= TTM_PL_FLAG_TOPDOWN;

	lbo->placement.placement = lbo->placements;

	if (domain & LSDC_GEM_DOMAIN_VRAM) {
		lbo->placements[c].mem_type = TTM_PL_VRAM;
		lbo->placements[c++].flags = pflags;
	}

	if (domain & LSDC_GEM_DOMAIN_GTT) {
		lbo->placements[c].mem_type = TTM_PL_TT;
		lbo->placements[c++].flags = pflags;
	}

	if (domain & LSDC_GEM_DOMAIN_SYSTEM) {
		lbo->placements[c].mem_type = TTM_PL_SYSTEM;
		lbo->placements[c++].flags = 0;
	}

	if (!c) {
		lbo->placements[c].mem_type = TTM_PL_SYSTEM;
		lbo->placements[c++].flags = 0;
	}

	lbo->placement.num_placement = c;

	for (i = 0; i < c; ++i) {
		lbo->placements[i].fpfn = 0;
		lbo->placements[i].lpfn = 0;
	}
}

static void lsdc_ttm_tt_destroy(struct ttm_device *bdev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static struct ttm_tt *
lsdc_ttm_tt_create(struct ttm_buffer_object *tbo, uint32_t page_flags)
{
	struct ttm_tt *tt;
	int ret;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	ret = ttm_sg_tt_init(tt, tbo, page_flags, ttm_cached);
	if (ret < 0) {
		kfree(tt);
		return NULL;
	}

	return tt;
}

static int lsdc_ttm_tt_populate(struct ttm_device *bdev,
				struct ttm_tt *ttm,
				struct ttm_operation_ctx *ctx)
{
	bool slave = !!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (slave && ttm->sg) {
		drm_prime_sg_to_dma_addr_array(ttm->sg,
					       ttm->dma_address,
					       ttm->num_pages);

		return 0;
	}

	return ttm_pool_alloc(&bdev->pool, ttm, ctx);
}

static void lsdc_ttm_tt_unpopulate(struct ttm_device *bdev,
				   struct ttm_tt *ttm)
{
	bool slave = !!(ttm->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (slave)
		return;

	return ttm_pool_free(&bdev->pool, ttm);
}

static void lsdc_bo_evict_flags(struct ttm_buffer_object *tbo,
				struct ttm_placement *tplacement)
{
	struct ttm_resource *resource = tbo->resource;
	struct lsdc_bo *lbo = to_lsdc_bo(tbo);

	switch (resource->mem_type) {
	case TTM_PL_VRAM:
		lsdc_bo_set_placement(lbo, LSDC_GEM_DOMAIN_GTT);
		break;
	case TTM_PL_TT:
	default:
		lsdc_bo_set_placement(lbo, LSDC_GEM_DOMAIN_SYSTEM);
		break;
	}

	*tplacement = lbo->placement;
}

static int lsdc_bo_move(struct ttm_buffer_object *tbo,
			bool evict,
			struct ttm_operation_ctx *ctx,
			struct ttm_resource *new_mem,
			struct ttm_place *hop)
{
	struct drm_device *ddev = tbo->base.dev;
	struct ttm_resource *old_mem = tbo->resource;
	struct lsdc_bo *lbo = to_lsdc_bo(tbo);
	int ret;

	if (unlikely(tbo->pin_count > 0)) {
		drm_warn(ddev, "Can't move a pinned BO\n");
		return -EINVAL;
	}

	ret = ttm_bo_wait_ctx(tbo, ctx);
	if (ret)
		return ret;

	if (!old_mem) {
		drm_dbg(ddev, "bo[%p] move: NULL to %s, size: %zu\n",
			lbo, lsdc_mem_type_to_str(new_mem->mem_type),
			lsdc_bo_size(lbo));
		ttm_bo_move_null(tbo, new_mem);
		return 0;
	}

	if (old_mem->mem_type == TTM_PL_SYSTEM && !tbo->ttm) {
		ttm_bo_move_null(tbo, new_mem);
		drm_dbg(ddev, "bo[%p] move: SYSTEM to NULL, size: %zu\n",
			lbo, lsdc_bo_size(lbo));
		return 0;
	}

	if (old_mem->mem_type == TTM_PL_SYSTEM &&
	    new_mem->mem_type == TTM_PL_TT) {
		drm_dbg(ddev, "bo[%p] move: SYSTEM to GTT, size: %zu\n",
			lbo, lsdc_bo_size(lbo));
		ttm_bo_move_null(tbo, new_mem);
		return 0;
	}

	if (old_mem->mem_type == TTM_PL_TT &&
	    new_mem->mem_type == TTM_PL_SYSTEM) {
		drm_dbg(ddev, "bo[%p] move: GTT to SYSTEM, size: %zu\n",
			lbo, lsdc_bo_size(lbo));
		ttm_resource_free(tbo, &tbo->resource);
		ttm_bo_assign_mem(tbo, new_mem);
		return 0;
	}

	drm_dbg(ddev, "bo[%p] move: %s to %s, size: %zu\n",
		lbo,
		lsdc_mem_type_to_str(old_mem->mem_type),
		lsdc_mem_type_to_str(new_mem->mem_type),
		lsdc_bo_size(lbo));

	return ttm_bo_move_memcpy(tbo, ctx, new_mem);
}

static int lsdc_bo_reserve_io_mem(struct ttm_device *bdev,
				  struct ttm_resource *mem)
{
	struct lsdc_device *ldev = tdev_to_ldev(bdev);

	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		break;
	case TTM_PL_TT:
		break;
	case TTM_PL_VRAM:
		mem->bus.offset = (mem->start << PAGE_SHIFT) + ldev->vram_base;
		mem->bus.is_iomem = true;
		mem->bus.caching = ttm_write_combined;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct ttm_device_funcs lsdc_bo_driver = {
	.ttm_tt_create = lsdc_ttm_tt_create,
	.ttm_tt_populate = lsdc_ttm_tt_populate,
	.ttm_tt_unpopulate = lsdc_ttm_tt_unpopulate,
	.ttm_tt_destroy = lsdc_ttm_tt_destroy,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = lsdc_bo_evict_flags,
	.move = lsdc_bo_move,
	.io_mem_reserve = lsdc_bo_reserve_io_mem,
};

u64 lsdc_bo_gpu_offset(struct lsdc_bo *lbo)
{
	struct ttm_buffer_object *tbo = &lbo->tbo;
	struct drm_device *ddev = tbo->base.dev;
	struct ttm_resource *resource = tbo->resource;

	if (unlikely(!tbo->pin_count)) {
		drm_err(ddev, "unpinned bo, gpu virtual address is invalid\n");
		return 0;
	}

	if (unlikely(resource->mem_type == TTM_PL_SYSTEM))
		return 0;

	return resource->start << PAGE_SHIFT;
}

size_t lsdc_bo_size(struct lsdc_bo *lbo)
{
	struct ttm_buffer_object *tbo = &lbo->tbo;

	return tbo->base.size;
}

int lsdc_bo_reserve(struct lsdc_bo *lbo)
{
	return ttm_bo_reserve(&lbo->tbo, true, false, NULL);
}

void lsdc_bo_unreserve(struct lsdc_bo *lbo)
{
	return ttm_bo_unreserve(&lbo->tbo);
}

int lsdc_bo_pin(struct lsdc_bo *lbo, u32 domain, u64 *gpu_addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct ttm_buffer_object *tbo = &lbo->tbo;
	struct lsdc_device *ldev = tdev_to_ldev(tbo->bdev);
	int ret;

	if (tbo->pin_count)
		goto bo_pinned;

	if (lbo->sharing_count && domain == LSDC_GEM_DOMAIN_VRAM)
		return -EINVAL;

	if (domain)
		lsdc_bo_set_placement(lbo, domain);

	ret = ttm_bo_validate(tbo, &lbo->placement, &ctx);
	if (unlikely(ret)) {
		drm_err(&ldev->base, "%p validate failed: %d\n", lbo, ret);
		return ret;
	}

	if (domain == LSDC_GEM_DOMAIN_VRAM)
		ldev->vram_pinned_size += lsdc_bo_size(lbo);
	else if (domain == LSDC_GEM_DOMAIN_GTT)
		ldev->gtt_pinned_size += lsdc_bo_size(lbo);

bo_pinned:
	ttm_bo_pin(tbo);

	if (gpu_addr)
		*gpu_addr = lsdc_bo_gpu_offset(lbo);

	return 0;
}

void lsdc_bo_unpin(struct lsdc_bo *lbo)
{
	struct ttm_buffer_object *tbo = &lbo->tbo;
	struct lsdc_device *ldev = tdev_to_ldev(tbo->bdev);

	if (unlikely(!tbo->pin_count)) {
		drm_dbg(&ldev->base, "%p unpin is not necessary\n", lbo);
		return;
	}

	ttm_bo_unpin(tbo);

	if (!tbo->pin_count) {
		if (tbo->resource->mem_type == TTM_PL_VRAM)
			ldev->vram_pinned_size -= lsdc_bo_size(lbo);
		else if (tbo->resource->mem_type == TTM_PL_TT)
			ldev->gtt_pinned_size -= lsdc_bo_size(lbo);
	}
}

void lsdc_bo_ref(struct lsdc_bo *lbo)
{
	drm_gem_object_get(&lbo->tbo.base);
}

void lsdc_bo_unref(struct lsdc_bo *lbo)
{
	drm_gem_object_put(&lbo->tbo.base);
}

int lsdc_bo_kmap(struct lsdc_bo *lbo)
{
	struct ttm_buffer_object *tbo = &lbo->tbo;
	struct drm_gem_object *gem = &tbo->base;
	struct drm_device *ddev = gem->dev;
	long ret;
	int err;

	ret = dma_resv_wait_timeout(gem->resv, DMA_RESV_USAGE_KERNEL, false,
				    MAX_SCHEDULE_TIMEOUT);
	if (ret < 0) {
		drm_warn(ddev, "wait fence timeout\n");
		return ret;
	}

	if (lbo->kptr)
		return 0;

	err = ttm_bo_kmap(tbo, 0, PFN_UP(lsdc_bo_size(lbo)), &lbo->kmap);
	if (err) {
		drm_err(ddev, "kmap %p failed: %d\n", lbo, err);
		return err;
	}

	lbo->kptr = ttm_kmap_obj_virtual(&lbo->kmap, &lbo->is_iomem);

	return 0;
}

void lsdc_bo_kunmap(struct lsdc_bo *lbo)
{
	if (!lbo->kptr)
		return;

	lbo->kptr = NULL;
	ttm_bo_kunmap(&lbo->kmap);
}

void lsdc_bo_clear(struct lsdc_bo *lbo)
{
	lsdc_bo_kmap(lbo);

	if (lbo->is_iomem)
		memset_io((void __iomem *)lbo->kptr, 0, lbo->size);
	else
		memset(lbo->kptr, 0, lbo->size);

	lsdc_bo_kunmap(lbo);
}

int lsdc_bo_evict_vram(struct drm_device *ddev)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct ttm_device *bdev = &ldev->bdev;
	struct ttm_resource_manager *man;

	man = ttm_manager_type(bdev, TTM_PL_VRAM);
	if (unlikely(!man))
		return 0;

	return ttm_resource_manager_evict_all(bdev, man);
}

static void lsdc_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct lsdc_device *ldev = tdev_to_ldev(tbo->bdev);
	struct lsdc_bo *lbo = to_lsdc_bo(tbo);

	mutex_lock(&ldev->gem.mutex);
	list_del_init(&lbo->list);
	mutex_unlock(&ldev->gem.mutex);

	drm_gem_object_release(&tbo->base);

	kfree(lbo);
}

struct lsdc_bo *lsdc_bo_create(struct drm_device *ddev,
			       u32 domain,
			       size_t size,
			       bool kernel,
			       struct sg_table *sg,
			       struct dma_resv *resv)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	struct ttm_device *bdev = &ldev->bdev;
	struct ttm_buffer_object *tbo;
	struct lsdc_bo *lbo;
	enum ttm_bo_type bo_type;
	int ret;

	lbo = kzalloc(sizeof(*lbo), GFP_KERNEL);
	if (!lbo)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&lbo->list);

	lbo->initial_domain = domain & (LSDC_GEM_DOMAIN_VRAM |
					LSDC_GEM_DOMAIN_GTT |
					LSDC_GEM_DOMAIN_SYSTEM);

	tbo = &lbo->tbo;

	size = ALIGN(size, PAGE_SIZE);

	ret = drm_gem_object_init(ddev, &tbo->base, size);
	if (ret) {
		kfree(lbo);
		return ERR_PTR(ret);
	}

	tbo->bdev = bdev;

	if (kernel)
		bo_type = ttm_bo_type_kernel;
	else if (sg)
		bo_type = ttm_bo_type_sg;
	else
		bo_type = ttm_bo_type_device;

	lsdc_bo_set_placement(lbo, domain);
	lbo->size = size;

	ret = ttm_bo_init_validate(bdev, tbo, bo_type, &lbo->placement, 0,
				   false, sg, resv, lsdc_bo_destroy);
	if (ret) {
		kfree(lbo);
		return ERR_PTR(ret);
	}

	return lbo;
}

struct lsdc_bo *lsdc_bo_create_kernel_pinned(struct drm_device *ddev,
					     u32 domain,
					     size_t size)
{
	struct lsdc_bo *lbo;
	int ret;

	lbo = lsdc_bo_create(ddev, domain, size, true, NULL, NULL);
	if (IS_ERR(lbo))
		return ERR_CAST(lbo);

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret)) {
		lsdc_bo_unref(lbo);
		return ERR_PTR(ret);
	}

	ret = lsdc_bo_pin(lbo, domain, NULL);
	lsdc_bo_unreserve(lbo);
	if (unlikely(ret)) {
		lsdc_bo_unref(lbo);
		return ERR_PTR(ret);
	}

	return lbo;
}

void lsdc_bo_free_kernel_pinned(struct lsdc_bo *lbo)
{
	int ret;

	ret = lsdc_bo_reserve(lbo);
	if (unlikely(ret))
		return;

	lsdc_bo_unpin(lbo);
	lsdc_bo_unreserve(lbo);

	lsdc_bo_unref(lbo);
}

static void lsdc_ttm_fini(struct drm_device *ddev, void *data)
{
	struct lsdc_device *ldev = (struct lsdc_device *)data;

	ttm_range_man_fini(&ldev->bdev, TTM_PL_VRAM);
	ttm_range_man_fini(&ldev->bdev, TTM_PL_TT);

	ttm_device_fini(&ldev->bdev);

	drm_dbg(ddev, "ttm finished\n");
}

int lsdc_ttm_init(struct lsdc_device *ldev)
{
	struct drm_device *ddev = &ldev->base;
	unsigned long num_vram_pages;
	unsigned long num_gtt_pages;
	int ret;

	ret = ttm_device_init(&ldev->bdev, &lsdc_bo_driver, ddev->dev,
			      ddev->anon_inode->i_mapping,
			      ddev->vma_offset_manager, false, true);
	if (ret)
		return ret;

	num_vram_pages = ldev->vram_size >> PAGE_SHIFT;

	ret = ttm_range_man_init(&ldev->bdev, TTM_PL_VRAM, false, num_vram_pages);
	if (unlikely(ret))
		return ret;

	drm_info(ddev, "VRAM: %lu pages ready\n", num_vram_pages);

	/* 512M is far enough for us now */
	ldev->gtt_size = 512 << 20;

	num_gtt_pages = ldev->gtt_size >> PAGE_SHIFT;

	ret = ttm_range_man_init(&ldev->bdev, TTM_PL_TT, true, num_gtt_pages);
	if (unlikely(ret))
		return ret;

	drm_info(ddev, "GTT: %lu pages ready\n", num_gtt_pages);

	return drmm_add_action_or_reset(ddev, lsdc_ttm_fini, ldev);
}

void lsdc_ttm_debugfs_init(struct lsdc_device *ldev)
{
	struct ttm_device *bdev = &ldev->bdev;
	struct drm_device *ddev = &ldev->base;
	struct drm_minor *minor = ddev->primary;
	struct dentry *root = minor->debugfs_root;
	struct ttm_resource_manager *vram_man;
	struct ttm_resource_manager *gtt_man;

	vram_man = ttm_manager_type(bdev, TTM_PL_VRAM);
	gtt_man = ttm_manager_type(bdev, TTM_PL_TT);

	ttm_resource_manager_create_debugfs(vram_man, root, "vram_mm");
	ttm_resource_manager_create_debugfs(gtt_man, root, "gtt_mm");
}
