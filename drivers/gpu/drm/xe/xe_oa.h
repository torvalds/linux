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

int xe_oa_init(struct xe_device *xe);
void xe_oa_fini(struct xe_device *xe);
void xe_oa_register(struct xe_device *xe);
void xe_oa_unregister(struct xe_device *xe);
int xe_oa_add_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file);
int xe_oa_remove_config_ioctl(struct drm_device *dev, u64 data, struct drm_file *file);

#endif
