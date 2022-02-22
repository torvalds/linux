// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_display_power_well.h"

void intel_power_well_enable(struct drm_i915_private *i915,
			     struct i915_power_well *power_well)
{
	drm_dbg_kms(&i915->drm, "enabling %s\n", power_well->desc->name);
	power_well->desc->ops->enable(i915, power_well);
	power_well->hw_enabled = true;
}

void intel_power_well_disable(struct drm_i915_private *i915,
			      struct i915_power_well *power_well)
{
	drm_dbg_kms(&i915->drm, "disabling %s\n", power_well->desc->name);
	power_well->hw_enabled = false;
	power_well->desc->ops->disable(i915, power_well);
}

void intel_power_well_get(struct drm_i915_private *i915,
			  struct i915_power_well *power_well)
{
	if (!power_well->count++)
		intel_power_well_enable(i915, power_well);
}

void intel_power_well_put(struct drm_i915_private *i915,
			  struct i915_power_well *power_well)
{
	drm_WARN(&i915->drm, !power_well->count,
		 "Use count on power well %s is already zero",
		 power_well->desc->name);

	if (!--power_well->count)
		intel_power_well_disable(i915, power_well);
}
