/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_VRAM_H_
#define _XE_VRAM_H_

#include <linux/types.h>

struct xe_device;
struct xe_vram_region;

void xe_vram_resize_bar(struct xe_device *xe);
int xe_vram_probe(struct xe_device *xe);

struct xe_vram_region *xe_vram_region_alloc(struct xe_device *xe, u8 id, u32 placement);

resource_size_t xe_vram_region_io_start(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_io_size(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_dpa_base(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_usable_size(const struct xe_vram_region *vram);
resource_size_t xe_vram_region_actual_physical_size(const struct xe_vram_region *vram);

#endif
