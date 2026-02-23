// SPDX-License-Identifier: MIT
/* Copyright © 2025 Intel Corporation */

#include <drm/drm_print.h>

#include "i915_drv.h"
#include "i915_edram.h"
#include "i915_reg.h"

static u32 gen9_edram_size_mb(struct drm_i915_private *i915, u32 cap)
{
	static const u8 ways[8] = { 4, 8, 12, 16, 16, 16, 16, 16 };
	static const u8 sets[4] = { 1, 1, 2, 2 };

	return EDRAM_NUM_BANKS(cap) *
		ways[EDRAM_WAYS_IDX(cap)] *
		sets[EDRAM_SETS_IDX(cap)];
}

void i915_edram_detect(struct drm_i915_private *i915)
{
	u32 edram_cap = 0;

	if (!(IS_HASWELL(i915) || IS_BROADWELL(i915) || GRAPHICS_VER(i915) >= 9))
		return;

	edram_cap = intel_uncore_read_fw(&i915->uncore, HSW_EDRAM_CAP);

	/* NB: We can't write IDICR yet because we don't have gt funcs set up */

	if (!(edram_cap & EDRAM_ENABLED))
		return;

	/*
	 * The needed capability bits for size calculation are not there with
	 * pre gen9 so return 128MB always.
	 */
	if (GRAPHICS_VER(i915) < 9)
		i915->edram_size_mb = 128;
	else
		i915->edram_size_mb = gen9_edram_size_mb(i915, edram_cap);

	drm_info(&i915->drm, "Found %uMB of eDRAM\n", i915->edram_size_mb);
}
