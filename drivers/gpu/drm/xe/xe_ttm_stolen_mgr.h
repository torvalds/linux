/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTM_STOLEN_MGR_H_
#define _XE_TTM_STOLEN_MGR_H_

#include <linux/types.h>

struct ttm_resource;
struct xe_bo;
struct xe_device;

void xe_ttm_stolen_mgr_init(struct xe_device *xe);
int xe_ttm_stolen_io_mem_reserve(struct xe_device *xe, struct ttm_resource *mem);
bool xe_ttm_stolen_cpu_access_needs_ggtt(struct xe_device *xe);
u64 xe_ttm_stolen_io_offset(struct xe_bo *bo, u32 offset);
u64 xe_ttm_stolen_gpu_offset(struct xe_device *xe);

#endif
