/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_GTT_MGR_TYPES_H_
#define _XE_TTM_GTT_MGR_TYPES_H_

#include <drm/ttm/ttm_device.h>

struct xe_gt;

struct xe_ttm_gtt_mgr {
	struct xe_gt *gt;
	struct ttm_resource_manager manager;
};

#endif
