/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_VRAM_MGR_H_
#define _XE_TTM_VRAM_MGR_H_

#include "xe_ttm_vram_mgr_types.h"

enum dma_data_direction;
struct xe_device;
struct xe_gt;

int xe_ttm_vram_mgr_init(struct xe_gt *gt, struct xe_ttm_vram_mgr *mgr);
int xe_ttm_vram_mgr_alloc_sgt(struct xe_device *xe,
			      struct ttm_resource *res,
			      u64 offset, u64 length,
			      struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table **sgt);
void xe_ttm_vram_mgr_free_sgt(struct device *dev, enum dma_data_direction dir,
			      struct sg_table *sgt);

static inline u64 xe_ttm_vram_mgr_block_start(struct drm_buddy_block *block)
{
	return drm_buddy_block_offset(block);
}

static inline u64 xe_ttm_vram_mgr_block_size(struct drm_buddy_block *block)
{
	return PAGE_SIZE << drm_buddy_block_order(block);
}

static inline struct xe_ttm_vram_mgr_resource *
to_xe_ttm_vram_mgr_resource(struct ttm_resource *res)
{
	return container_of(res, struct xe_ttm_vram_mgr_resource, base);
}

#endif
