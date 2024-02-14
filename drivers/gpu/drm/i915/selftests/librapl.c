// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <asm/msr.h>

#include "i915_drv.h"
#include "librapl.h"

bool librapl_supported(const struct drm_i915_private *i915)
{
	/* Discrete cards require hwmon integration */
	if (IS_DGFX(i915))
		return false;

	return librapl_energy_uJ();
}

u64 librapl_energy_uJ(void)
{
	unsigned long long power;
	u32 units;

	if (rdmsrl_safe(MSR_RAPL_POWER_UNIT, &power))
		return 0;

	units = (power & 0x1f00) >> 8;

	if (rdmsrl_safe(MSR_PP1_ENERGY_STATUS, &power))
		return 0;

	return (1000000 * power) >> units; /* convert to uJ */
}
