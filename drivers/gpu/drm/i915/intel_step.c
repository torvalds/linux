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

const struct i915_rev_steppings kbl_revids[] = {
	[0] = { .gt_stepping = KBL_REVID_A0, .disp_stepping = KBL_REVID_A0 },
	[1] = { .gt_stepping = KBL_REVID_B0, .disp_stepping = KBL_REVID_B0 },
	[2] = { .gt_stepping = KBL_REVID_C0, .disp_stepping = KBL_REVID_B0 },
	[3] = { .gt_stepping = KBL_REVID_D0, .disp_stepping = KBL_REVID_B0 },
	[4] = { .gt_stepping = KBL_REVID_F0, .disp_stepping = KBL_REVID_C0 },
	[5] = { .gt_stepping = KBL_REVID_C0, .disp_stepping = KBL_REVID_B1 },
	[6] = { .gt_stepping = KBL_REVID_D1, .disp_stepping = KBL_REVID_B1 },
	[7] = { .gt_stepping = KBL_REVID_G0, .disp_stepping = KBL_REVID_C0 },
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
