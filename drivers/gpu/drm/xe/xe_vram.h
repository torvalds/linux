/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_VRAM_H_
#define _XE_VRAM_H_

#include <linux/types.h>

struct xe_device;
struct xe_vram_region;
struct ttm_resource;

struct xe_vram_region *xe_map_resource_to_region(struct ttm_resource *res);
int xe_vram_probe(struct xe_device *xe);

struct xe_vram_region *xe_vram_region_alloc(struct xe_device *xe, u8 id, u32 placement);

resource_size_t xe_vram_region_io_start(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_io_size(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_dpa_base(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_usable_size(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_actual_physical_size(const struct xe_vram_region *vram);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
int xe_vram_reserve_memtest_bo(struct xe_device *xe);
void xe_vram_free_memtest_bos(struct xe_device *xe);
int xe_vram_memtest(struct xe_device *xe);
#else
static inline int xe_vram_reserve_memtest_bo(struct xe_device *xe) { return 0; }
static inline void xe_vram_free_memtest_bos(struct xe_device *xe) {}
static inline int xe_vram_memtest(struct xe_device *xe) { return 0; }
#endif

#endif
