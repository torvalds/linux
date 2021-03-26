// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_step.h"

/*
 * KBL revision ID ordering is bizarre; higher revision ID's map to lower
 * steppings in some cases.  So rather than test against the revision ID
 * directly, let's map that into our own range of increasing ID's that we
 * can test against in a regular manner.
 */


/* FIXME: what about REVID_E0 */
static const struct i915_rev_steppings kbl_revids[] = {
	[0] = { .gt_stepping = STEP_A0, .disp_stepping = STEP_A0 },
	[1] = { .gt_stepping = STEP_B0, .disp_stepping = STEP_B0 },
	[2] = { .gt_stepping = STEP_C0, .disp_stepping = STEP_B0 },
	[3] = { .gt_stepping = STEP_D0, .disp_stepping = STEP_B0 },
	[4] = { .gt_stepping = STEP_F0, .disp_stepping = STEP_C0 },
	[5] = { .gt_stepping = STEP_C0, .disp_stepping = STEP_B1 },
	[6] = { .gt_stepping = STEP_D1, .disp_stepping = STEP_B1 },
	[7] = { .gt_stepping = STEP_G0, .disp_stepping = STEP_C0 },
};

const struct i915_rev_steppings tgl_uy_revid_step_tbl[] = {
	[0] = { .gt_stepping = STEP_A0, .disp_stepping = STEP_A0 },
	[1] = { .gt_stepping = STEP_B0, .disp_stepping = STEP_C0 },
	[2] = { .gt_stepping = STEP_B1, .disp_stepping = STEP_C0 },
	[3] = { .gt_stepping = STEP_C0, .disp_stepping = STEP_D0 },
};

/* Same GT stepping between tgl_uy_revids and tgl_revids don't mean the same HW */
const struct i915_rev_steppings tgl_revid_step_tbl[] = {
	[0] = { .gt_stepping = STEP_A0, .disp_stepping = STEP_B0 },
	[1] = { .gt_stepping = STEP_B0, .disp_stepping = STEP_D0 },
};

const struct i915_rev_steppings adls_revid_step_tbl[] = {
	[0x0] = { .gt_stepping = STEP_A0, .disp_stepping = STEP_A0 },
	[0x1] = { .gt_stepping = STEP_A0, .disp_stepping = STEP_A2 },
	[0x4] = { .gt_stepping = STEP_B0, .disp_stepping = STEP_B0 },
	[0x8] = { .gt_stepping = STEP_C0, .disp_stepping = STEP_B0 },
	[0xC] = { .gt_stepping = STEP_D0, .disp_stepping = STEP_C0 },
};

void intel_step_init(struct drm_i915_private *i915)
{
	const struct i915_rev_steppings *revids = NULL;
	int size = 0;
	int revid = INTEL_REVID(i915);
	struct i915_rev_steppings step = {};

	if (IS_KABYLAKE(i915)) {
		revids = kbl_revids;
		size = ARRAY_SIZE(kbl_revids);
	}

	/* Not using the stepping scheme for the platform yet. */
	if (!revids)
		return;

	if (revid < size && revids[revid].gt_stepping != STEP_NONE) {
		step = revids[revid];
	} else {
		drm_warn(&i915->drm, "Unknown revid 0x%02x\n", revid);

		/*
		 * If we hit a gap in the revid array, use the information for
		 * the next revid.
		 *
		 * This may be wrong in all sorts of ways, especially if the
		 * steppings in the array are not monotonically increasing, but
		 * it's better than defaulting to 0.
		 */
		while (revid < size && revids[revid].gt_stepping == STEP_NONE)
			revid++;

		if (revid < size) {
			drm_dbg(&i915->drm, "Using steppings for revid 0x%02x\n",
				revid);
			step = revids[revid];
		} else {
			drm_dbg(&i915->drm, "Using future steppings\n");
			step.gt_stepping = STEP_FUTURE;
			step.disp_stepping = STEP_FUTURE;
		}
	}

	if (drm_WARN_ON(&i915->drm, step.gt_stepping == STEP_NONE))
		return;

	RUNTIME_INFO(i915)->step = step;
}
