/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef __GEN8_PPGTT_H__
#define __GEN8_PPGTT_H__

struct intel_gt;

struct i915_ppgtt *gen8_ppgtt_create(struct intel_gt *gt);

#endif
