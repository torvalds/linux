/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __I915_PERF_H__
#define __I915_PERF_H__

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_i915_private;
struct intel_context;
struct intel_engine_cs;

void i915_perf_init(struct drm_i915_private *i915);
void i915_perf_fini(struct drm_i915_private *i915);
void i915_perf_register(struct drm_i915_private *i915);
void i915_perf_unregister(struct drm_i915_private *i915);

int i915_perf_open_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file);
int i915_perf_add_config_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file);
int i915_perf_remove_config_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file);
void i915_oa_init_reg_state(struct intel_engine_cs *engine,
			    struct intel_context *ce,
			    u32 *reg_state);

#endif /* __I915_PERF_H__ */
