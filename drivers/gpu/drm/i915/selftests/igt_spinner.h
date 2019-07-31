/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#ifndef __I915_SELFTESTS_IGT_SPINNER_H__
#define __I915_SELFTESTS_IGT_SPINNER_H__

#include "gem/i915_gem_context.h"
#include "gt/intel_engine.h"

#include "i915_drv.h"
#include "i915_request.h"
#include "i915_selftest.h"

struct intel_gt;

struct igt_spinner {
	struct intel_gt *gt;
	struct drm_i915_gem_object *hws;
	struct drm_i915_gem_object *obj;
	u32 *batch;
	void *seqno;
};

int igt_spinner_init(struct igt_spinner *spin, struct intel_gt *gt);
void igt_spinner_fini(struct igt_spinner *spin);

struct i915_request *
igt_spinner_create_request(struct igt_spinner *spin,
			   struct intel_context *ce,
			   u32 arbitration_command);
void igt_spinner_end(struct igt_spinner *spin);

bool igt_wait_for_spinner(struct igt_spinner *spin, struct i915_request *rq);

#endif
