/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __XE_EU_STALL_H__
#define __XE_EU_STALL_H__

#include "xe_gt_types.h"

int xe_eu_stall_stream_open(struct drm_device *dev,
			    u64 data,
			    struct drm_file *file);
#endif
