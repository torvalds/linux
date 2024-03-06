/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_VRAM_MGR_TYPES_H_
#define _XE_TTM_VRAM_MGR_TYPES_H_

#include <drm/drm_buddy.h>
#include <drm/ttm/ttm_device.h>

struct xe_mem_region;

/**
 * struct xe_ttm_vram_mgr - XE TTM VRAM manager
 *
 * Manages placement of TTM resource in VRAM.
 */
struct xe_ttm_vram_mgr {
	/** @manager: Base TTM resource manager */
	struct ttm_resource_manager manager;
	/** @mm: DRM buddy allocator which manages the VRAM */
	struct drm_buddy mm;
	/** @vram: ptr to details of associated VRAM region */
	struct xe_mem_region *vram;
	/** @visible_size: Proped size of the CPU visible portion */
	u64 visible_size;
	/** @visible_avail: CPU visible portion still unallocated */
	u64 visible_avail;
	/** @default_page_size: default page size */
	u64 default_page_size;
	/** @lock: protects allocations of VRAM */
	struct mutex lock;
	/** @mem_type: The TTM memory type */
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
	/** @used_visible_size: How many CPU visible bytes this resource is using */
	u64 used_visible_size;
	/** @flags: flags associated with the resource */
	unsigned long flags;
};

#endif
