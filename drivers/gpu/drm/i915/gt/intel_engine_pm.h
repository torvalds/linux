/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_ENGINE_PM_H
#define INTEL_ENGINE_PM_H

#include "intel_engine_types.h"
#include "intel_wakeref.h"

struct drm_i915_private;
struct intel_engine_cs;

void intel_engine_pm_get(struct intel_engine_cs *engine);
void intel_engine_pm_put(struct intel_engine_cs *engine);

static inline bool
intel_engine_pm_is_awake(const struct intel_engine_cs *engine)
{
	return intel_wakeref_is_active(&engine->wakeref);
}

void intel_engine_park(struct intel_engine_cs *engine);

void intel_engine_init__pm(struct intel_engine_cs *engine);

int intel_engines_resume(struct drm_i915_private *i915);

#endif /* INTEL_ENGINE_PM_H */
