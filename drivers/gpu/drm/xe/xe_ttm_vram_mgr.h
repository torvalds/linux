/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_VRAM_MGR_H_
#define _XE_TTM_VRAM_MGR_H_

#include "xe_ttm_vram_mgr_types.h"

enum dma_data_direction;
struct xe_device;
struct xe_tile;
struct xe_vram_region;

int __xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_ttm_vram_mgr *mgr,
			   u32 mem_type, u64 size, u64 io_size,
			   u64 default_page_size);
int xe_ttm_vram_mgr_init(struct xe_device *xe, struct xe_vram_region *vram);
int xe_ttm_vram_mgr_alloc_sgt(struct xe_device *xe,
			      struct ttm_resource *res,
			      u64 offset, u64 length,
			      struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table **sgt);
void xe_ttm_vram_mgr_free_sgt(struct device *dev, enum dma_data_direction dir,
			      struct sg_table *sgt);

u64 xe_ttm_vram_get_avail(struct ttm_resource_manager *man);
u64 xe_ttm_vram_get_cpu_visible_size(struct ttm_resource_manager *man);
void xe_ttm_vram_get_used(struct ttm_resource_manager *man,
			  u64 *used, u64 *used_visible);

static inline struct xe_ttm_vram_mgr_resource *
to_xe_ttm_vram_mgr_resource(struct ttm_resource *res)
{
	return container_of(res, struct xe_ttm_vram_mgr_resource, base);
}

static inline struct xe_ttm_vram_mgr *
to_xe_ttm_vram_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct xe_ttm_vram_mgr, manager);
}

#endif
