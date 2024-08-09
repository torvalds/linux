/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_HW_ENGINE_GROUP_H_
#define _XE_HW_ENGINE_GROUP_H_

#include "xe_hw_engine_group_types.h"

struct drm_device;
struct xe_gt;

int xe_hw_engine_setup_groups(struct xe_gt *gt);

#endif
