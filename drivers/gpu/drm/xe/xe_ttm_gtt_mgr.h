/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_TTGM_GTT_MGR_H_
#define _XE_TTGM_GTT_MGR_H_

#include "xe_ttm_gtt_mgr_types.h"

struct xe_gt;

int xe_ttm_gtt_mgr_init(struct xe_gt *gt, struct xe_ttm_gtt_mgr *mgr,
			u64 gtt_size);

#endif
