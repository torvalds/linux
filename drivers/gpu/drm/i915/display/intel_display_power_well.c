// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_display_power_well.h"

struct i915_power_well *
lookup_power_well(struct drm_i915_private *i915,
		  enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;

	for_each_power_well(i915, power_well)
		if (power_well->desc->id == power_well_id)
			return power_well;

	/*
	 * It's not feasible to add error checking code to the callers since
	 * this condition really shouldn't happen and it doesn't even make sense
	 * to abort things like display initialization sequences. Just return
	 * the first power well and hope the WARN gets reported so we can fix
	 * our driver.
	 */
	drm_WARN(&i915->drm, 1,
		 "Power well %d not defined for this platform\n",
		 power_well_id);
	return &i915->power_domains.power_wells[0];
}

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

void intel_power_well_sync_hw(struct drm_i915_private *i915,
			      struct i915_power_well *power_well)
{
	power_well->desc->ops->sync_hw(i915, power_well);
	power_well->hw_enabled =
		power_well->desc->ops->is_enabled(i915, power_well);
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

bool intel_power_well_is_enabled(struct drm_i915_private *i915,
				 struct i915_power_well *power_well)
{
	return power_well->desc->ops->is_enabled(i915, power_well);
}

bool intel_power_well_is_enabled_cached(struct i915_power_well *power_well)
{
	return power_well->hw_enabled;
}

bool intel_display_power_well_is_enabled(struct drm_i915_private *dev_priv,
					 enum i915_power_well_id power_well_id)
{
	struct i915_power_well *power_well;

	power_well = lookup_power_well(dev_priv, power_well_id);

	return intel_power_well_is_enabled(dev_priv, power_well);
}

bool intel_power_well_is_always_on(struct i915_power_well *power_well)
{
	return power_well->desc->always_on;
}

const char *intel_power_well_name(struct i915_power_well *power_well)
{
	return power_well->desc->name;
}

u64 intel_power_well_domains(struct i915_power_well *power_well)
{
	return power_well->desc->domains;
}

int intel_power_well_refcount(struct i915_power_well *power_well)
{
	return power_well->count;
}
