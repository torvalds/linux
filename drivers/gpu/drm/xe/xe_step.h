/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_STEP_H_
#define _XE_STEP_H_

#include <linux/types.h>

#include "xe_step_types.h"

struct xe_device;

struct xe_step_info xe_step_get(struct xe_device *xe);
const char *xe_step_name(enum xe_step step);

#endif
