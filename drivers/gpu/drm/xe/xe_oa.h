/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_OA_H_
#define _XE_OA_H_

#include "xe_oa_types.h"

struct xe_device;

int xe_oa_init(struct xe_device *xe);
void xe_oa_fini(struct xe_device *xe);

#endif
