/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_USER_H
#define INTEL_ENGINE_USER_H

#include <linux/types.h>

struct drm_i915_private;
struct intel_engine_cs;

struct intel_engine_cs *
intel_engine_lookup_user(struct drm_i915_private *i915, u8 class, u8 instance);

unsigned int intel_engines_has_context_isolation(struct drm_i915_private *i915);

void intel_engine_add_user(struct intel_engine_cs *engine);
void intel_engines_driver_register(struct drm_i915_private *i915);

const char *intel_engine_class_repr(u8 class);

#endif /* INTEL_ENGINE_USER_H */
