/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef __INTEL_DISPLAY_POWER_MAP_H__
#define __INTEL_DISPLAY_POWER_MAP_H__

struct i915_power_domains;

int intel_display_power_map_init(struct i915_power_domains *power_domains);
void intel_display_power_map_cleanup(struct i915_power_domains *power_domains);

#endif
