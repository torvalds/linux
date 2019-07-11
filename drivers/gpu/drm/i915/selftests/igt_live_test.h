/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef IGT_LIVE_TEST_H
#define IGT_LIVE_TEST_H

#include "../i915_gem.h"

struct drm_i915_private;

struct igt_live_test {
	struct drm_i915_private *i915;
	const char *func;
	const char *name;

	unsigned int reset_global;
	unsigned int reset_engine[I915_NUM_ENGINES];
};

/*
 * Flush the GPU state before and after the test to ensure that no residual
 * code is running on the GPU that may affect this test. Also compare the
 * state before and after the test and alert if it unexpectedly changes,
 * e.g. if the GPU was reset.
 */
int igt_live_test_begin(struct igt_live_test *t,
			struct drm_i915_private *i915,
			const char *func,
			const char *name);
int igt_live_test_end(struct igt_live_test *t);

#endif /* IGT_LIVE_TEST_H */
