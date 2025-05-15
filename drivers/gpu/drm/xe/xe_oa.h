/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_OA_H_
#define _XE_OA_H_

#include "xe_oa_types.h"

struct drm_device;
struct drm_file;
struct xe_device;
struct xe_gt;
struct xe_hw_engine;

int xe_oa_init(struct xe_device *xe);
int xe_oa_register(struct xe_device *xe);
int xe_oa_stream_open_ioctl(struct drm_device *dev, u64 data, struct drm_file *file);
int xe_oa_add_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file);
int xe_oa_remove_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file);
u32 xe_oa_timestamp_frequency(struct xe_gt *gt);
u16 xe_oa_unit_id(struct xe_hw_engine *hwe);

#endif
