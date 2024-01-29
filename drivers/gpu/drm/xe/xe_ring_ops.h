/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_RING_OPS_H_
#define _XE_RING_OPS_H_

#include "xe_hw_engine_types.h"
#include "xe_ring_ops_types.h"

struct xe_gt;

const struct xe_ring_ops *
xe_ring_ops_get(struct xe_gt *gt, enum xe_engine_class class);

#endif
