/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_VRAM_TYPES_H_
#define _XE_VRAM_TYPES_H_

#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)
#include <drm/drm_pagemap.h>
#endif

#include "xe_ttm_vram_mgr_types.h"

struct xe_device;
struct xe_migrate;

/**
 * struct xe_vram_region - memory region structure
 * This is used to describe a memory region in xe
 * device, such as HBM memory or CXL extension memory.
 */
struct xe_vram_region {
	/** @xe: Back pointer to xe device */
	struct xe_device *xe;
	/**
	 * @id: VRAM region instance id
	 *
	 * The value should be unique for VRAM region.
	 */
	u8 id;
	/** @io_start: IO start address of this VRAM instance */
	resource_size_t io_start;
	/**
	 * @io_size: IO size of this VRAM instance
	 *
	 * This represents how much of this VRAM we can access
	 * via the CPU through the VRAM BAR. This can be smaller
	 * than @usable_size, in which case only part of VRAM is CPU
	 * accessible (typically the first 256M). This
	 * configuration is known as small-bar.
	 */
	resource_size_t io_size;
	/** @dpa_base: This memory regions's DPA (device physical address) base */
	resource_size_t dpa_base;
	/**
	 * @usable_size: usable size of VRAM
	 *
	 * Usable size of VRAM excluding reserved portions
	 * (e.g stolen mem)
	 */
	resource_size_t usable_size;
	/**
	 * @actual_physical_size: Actual VRAM size
	 *
	 * Actual VRAM size including reserved portions
	 * (e.g stolen mem)
	 */
	resource_size_t actual_physical_size;
	/** @mapping: pointer to VRAM mappable space */
	void __iomem *mapping;
	/** @ttm: VRAM TTM manager */
	struct xe_ttm_vram_mgr ttm;
	/** @placement: TTM placement dedicated for this region */
	u32 placement;
#if IS_ENABLED(CONFIG_DRM_XE_PAGEMAP)
	/** @migrate: Back pointer to migrate */
	struct xe_migrate *migrate;
	/** @pagemap: Used to remap device memory as ZONE_DEVICE */
	struct dev_pagemap pagemap;
	/**
	 * @dpagemap: The struct drm_pagemap of the ZONE_DEVICE memory
	 * pages of this tile.
	 */
	struct drm_pagemap dpagemap;
	/**
	 * @hpa_base: base host physical address
	 *
	 * This is generated when remap device memory as ZONE_DEVICE
	 */
	resource_size_t hpa_base;
#endif
};

#endif
