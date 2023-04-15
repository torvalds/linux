/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_HW_ENGINE_H_
#define _XE_HW_ENGINE_H_

#include "xe_hw_engine_types.h"

struct drm_printer;

int xe_hw_engines_init_early(struct xe_gt *gt);
int xe_hw_engines_init(struct xe_gt *gt);
void xe_hw_engine_handle_irq(struct xe_hw_engine *hwe, u16 intr_vec);
void xe_hw_engine_enable_ring(struct xe_hw_engine *hwe);
void xe_hw_engine_print_state(struct xe_hw_engine *hwe, struct drm_printer *p);
u32 xe_hw_engine_mask_per_class(struct xe_gt *gt,
				enum xe_engine_class engine_class);
void xe_hw_engine_setup_default_lrc_state(struct xe_hw_engine *hwe);

bool xe_hw_engine_is_reserved(struct xe_hw_engine *hwe);
static inline bool xe_hw_engine_is_valid(struct xe_hw_engine *hwe)
{
	return hwe->name;
}

#endif
