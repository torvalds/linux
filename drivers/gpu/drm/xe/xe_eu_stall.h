/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __XE_EU_STALL_H__
#define __XE_EU_STALL_H__

#include "xe_gt_types.h"
#include "xe_sriov.h"

size_t xe_eu_stall_get_per_xecore_buf_size(void);
size_t xe_eu_stall_data_record_size(struct xe_device *xe);
size_t xe_eu_stall_get_sampling_rates(u32 *num_rates, const u64 **rates);

int xe_eu_stall_init(struct xe_gt *gt);
int xe_eu_stall_stream_open(struct drm_device *dev,
			    u64 data,
			    struct drm_file *file);

static inline bool xe_eu_stall_supported_on_platform(struct xe_device *xe)
{
	return !IS_SRIOV_VF(xe) && (xe->info.platform == XE_PVC || GRAPHICS_VER(xe) >= 20);
}
#endif
