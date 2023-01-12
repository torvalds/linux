/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_VRAM_MGR_TYPES_H_
#define _XE_TTM_VRAM_MGR_TYPES_H_

#include <drm/drm_buddy.h>
#include <drm/ttm/ttm_device.h>

struct xe_gt;

/**
 * struct xe_ttm_vram_mgr - XE TTM VRAM manager
 *
 * Manages placement of TTM resource in VRAM.
 */
struct xe_ttm_vram_mgr {
	/** @gt: Graphics tile which the VRAM belongs to */
	struct xe_gt *gt;
	/** @manager: Base TTM resource manager */
	struct ttm_resource_manager manager;
	/** @mm: DRM buddy allocator which manages the VRAM */
	struct drm_buddy mm;
	/** @default_page_size: default page size */
	u64 default_page_size;
	/** @lock: protects allocations of VRAM */
	struct mutex lock;

	u32 mem_type;
};

/**
 * struct xe_ttm_vram_mgr_resource - XE TTM VRAM resource
 */
struct xe_ttm_vram_mgr_resource {
	/** @base: Base TTM resource */
	struct ttm_resource base;
	/** @blocks: list of DRM buddy blocks */
	struct list_head blocks;
	/** @flags: flags associated with the resource */
	unsigned long flags;
};

#endif
