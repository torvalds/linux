/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_TTM_H__
#define __LSDC_TTM_H__

#include <linux/container_of.h>
#include <linux/iosys-map.h>
#include <linux/list.h>

#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_range_manager.h>
#include <drm/ttm/ttm_tt.h>

#define LSDC_GEM_DOMAIN_SYSTEM          0x1
#define LSDC_GEM_DOMAIN_GTT             0x2
#define LSDC_GEM_DOMAIN_VRAM            0x4

struct lsdc_bo {
	struct ttm_buffer_object tbo;

	/* Protected by gem.mutex */
	struct list_head list;

	struct iosys_map map;

	unsigned int vmap_count;
	/* cross device driver sharing reference count */
	unsigned int sharing_count;

	struct ttm_bo_kmap_obj kmap;
	void *kptr;
	bool is_iomem;

	size_t size;

	u32 initial_domain;

	struct ttm_placement placement;
	struct ttm_place placements[4];
};

static inline struct ttm_buffer_object *to_ttm_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct ttm_buffer_object, base);
}

static inline struct lsdc_bo *to_lsdc_bo(struct ttm_buffer_object *tbo)
{
	return container_of(tbo, struct lsdc_bo, tbo);
}

static inline struct lsdc_bo *gem_to_lsdc_bo(struct drm_gem_object *gem)
{
	return container_of(gem, struct lsdc_bo, tbo.base);
}

const char *lsdc_mem_type_to_str(uint32_t mem_type);
const char *lsdc_domain_to_str(u32 domain);

struct lsdc_bo *lsdc_bo_create(struct drm_device *ddev,
			       u32 domain,
			       size_t size,
			       bool kernel,
			       struct sg_table *sg,
			       struct dma_resv *resv);

struct lsdc_bo *lsdc_bo_create_kernel_pinned(struct drm_device *ddev,
					     u32 domain,
					     size_t size);

void lsdc_bo_free_kernel_pinned(struct lsdc_bo *lbo);

int lsdc_bo_reserve(struct lsdc_bo *lbo);
void lsdc_bo_unreserve(struct lsdc_bo *lbo);

int lsdc_bo_pin(struct lsdc_bo *lbo, u32 domain, u64 *gpu_addr);
void lsdc_bo_unpin(struct lsdc_bo *lbo);

void lsdc_bo_ref(struct lsdc_bo *lbo);
void lsdc_bo_unref(struct lsdc_bo *lbo);

u64 lsdc_bo_gpu_offset(struct lsdc_bo *lbo);
size_t lsdc_bo_size(struct lsdc_bo *lbo);

int lsdc_bo_kmap(struct lsdc_bo *lbo);
void lsdc_bo_kunmap(struct lsdc_bo *lbo);
void lsdc_bo_clear(struct lsdc_bo *lbo);

int lsdc_bo_evict_vram(struct drm_device *ddev);

int lsdc_ttm_init(struct lsdc_device *ldev);
void lsdc_ttm_debugfs_init(struct lsdc_device *ldev);

#endif
