/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2008-2018 Intel Corporation
 */

#ifndef I915_RESET_H
#define I915_RESET_H

#include <linux/compiler.h>
#include <linux/types.h>

struct drm_i915_private;
struct intel_engine_cs;
struct intel_guc;

__printf(4, 5)
void i915_handle_error(struct drm_i915_private *i915,
		       u32 engine_mask,
		       unsigned long flags,
		       const char *fmt, ...);
#define I915_ERROR_CAPTURE BIT(0)

void i915_clear_error_registers(struct drm_i915_private *i915);

void i915_reset(struct drm_i915_private *i915,
		unsigned int stalled_mask,
		const char *reason);
int i915_reset_engine(struct intel_engine_cs *engine,
		      const char *reason);

void i915_reset_request(struct i915_request *rq, bool guilty);
bool i915_reset_flush(struct drm_i915_private *i915);

bool intel_has_gpu_reset(struct drm_i915_private *i915);
bool intel_has_reset_engine(struct drm_i915_private *i915);

int intel_gpu_reset(struct drm_i915_private *i915, u32 engine_mask);

int intel_reset_guc(struct drm_i915_private *i915);

struct i915_wedge_me {
	struct delayed_work work;
	struct drm_i915_private *i915;
	const char *name;
};

void __i915_init_wedge(struct i915_wedge_me *w,
		       struct drm_i915_private *i915,
		       long timeout,
		       const char *name);
void __i915_fini_wedge(struct i915_wedge_me *w);

#define i915_wedge_on_timeout(W, DEV, TIMEOUT)				\
	for (__i915_init_wedge((W), (DEV), (TIMEOUT), __func__);	\
	     (W)->i915;							\
	     __i915_fini_wedge((W)))

#endif /* I915_RESET_H */
