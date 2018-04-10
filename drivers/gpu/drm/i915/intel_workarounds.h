/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef _I915_WORKAROUNDS_H_
#define _I915_WORKAROUNDS_H_

int init_workarounds_ring(struct intel_engine_cs *engine);
int intel_ring_workarounds_emit(struct i915_request *rq);

#endif
