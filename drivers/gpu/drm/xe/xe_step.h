/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_STEP_H_
#define _XE_STEP_H_

#include <linux/types.h>

#include "xe_step_types.h"

struct xe_device;

struct xe_step_info xe_step_pre_gmdid_get(struct xe_device *xe);
struct xe_step_info xe_step_gmdid_get(struct xe_device *xe,
				      u32 graphics_gmdid_revid,
				      u32 media_gmdid_revid);
static inline u32 xe_step_to_gmdid(enum xe_step step) { return step - STEP_A0; }

const char *xe_step_name(enum xe_step step);

#endif
