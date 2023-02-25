// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_step.h"

#include <linux/bitfield.h>

#include "xe_device.h"
#include "xe_platform_types.h"

/*
 * Provide mapping between PCI's revision ID to the individual GMD
 * (Graphics/Media/Display) stepping values that can be compared numerically.
 *
 * Some platforms may have unusual ways of mapping PCI revision ID to GMD
 * steppings.  E.g., in some cases a higher PCI revision may translate to a
 * lower stepping of the GT and/or display IP.
 *
 * Also note that some revisions/steppings may have been set aside as
 * placeholders but never materialized in real hardware; in those cases there
 * may be jumps in the revision IDs or stepping values in the tables below.
 */

/*
 * Some platforms always have the same stepping value for GT and display;
 * use a macro to define these to make it easier to identify the platforms
 * where the two steppings can deviate.
 */
#define COMMON_GT_MEDIA_STEP(x_)	\
	.graphics = STEP_##x_,		\
	.media = STEP_##x_

#define COMMON_STEP(x_)			\
	COMMON_GT_MEDIA_STEP(x_),	\
	.graphics = STEP_##x_,		\
	.media = STEP_##x_,		\
	.display = STEP_##x_

__diag_push();
__diag_ignore_all("-Woverride-init", "Allow field overrides in table");

/* Same GT stepping between tgl_uy_revids and tgl_revids don't mean the same HW */
static const struct xe_step_info tgl_revids[] = {
	[0] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_B0 },
	[1] = { COMMON_GT_MEDIA_STEP(B0), .display = STEP_D0 },
};

static const struct xe_step_info dg1_revids[] = {
	[0] = { COMMON_STEP(A0) },
	[1] = { COMMON_STEP(B0) },
};

static const struct xe_step_info adls_revids[] = {
	[0x0] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_A0 },
	[0x1] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_A2 },
	[0x4] = { COMMON_GT_MEDIA_STEP(B0), .display = STEP_B0 },
	[0x8] = { COMMON_GT_MEDIA_STEP(C0), .display = STEP_B0 },
	[0xC] = { COMMON_GT_MEDIA_STEP(D0), .display = STEP_C0 },
};

static const struct xe_step_info dg2_g10_revid_step_tbl[] = {
	[0x0] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_A0 },
	[0x1] = { COMMON_GT_MEDIA_STEP(A1), .display = STEP_A0 },
	[0x4] = { COMMON_GT_MEDIA_STEP(B0), .display = STEP_B0 },
	[0x8] = { COMMON_GT_MEDIA_STEP(C0), .display = STEP_C0 },
};

static const struct xe_step_info dg2_g11_revid_step_tbl[] = {
	[0x0] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_B0 },
	[0x4] = { COMMON_GT_MEDIA_STEP(B0), .display = STEP_C0 },
	[0x5] = { COMMON_GT_MEDIA_STEP(B1), .display = STEP_C0 },
};

static const struct xe_step_info dg2_g12_revid_step_tbl[] = {
	[0x0] = { COMMON_GT_MEDIA_STEP(A0), .display = STEP_C0 },
	[0x1] = { COMMON_GT_MEDIA_STEP(A1), .display = STEP_C0 },
};

static const struct xe_step_info pvc_revid_step_tbl[] = {
	[0x3] = { .graphics = STEP_A0 },
	[0x5] = { .graphics = STEP_B0 },
	[0x6] = { .graphics = STEP_B1 },
	[0x7] = { .graphics = STEP_C0 },
};

static const int pvc_basedie_subids[] = {
	[0x0] = STEP_A0,
	[0x3] = STEP_B0,
	[0x4] = STEP_B1,
	[0x5] = STEP_B3,
};

__diag_pop();

struct xe_step_info xe_step_get(struct xe_device *xe)
{
	const struct xe_step_info *revids = NULL;
	struct xe_step_info step = {};
	u16 revid = xe->info.revid;
	int size = 0;
	const int *basedie_info = NULL;
	int basedie_size = 0;
	int baseid = 0;

	if (xe->info.platform == XE_PVC) {
		baseid = FIELD_GET(GENMASK(5, 3), xe->info.revid);
		revid = FIELD_GET(GENMASK(2, 0), xe->info.revid);
		revids = pvc_revid_step_tbl;
		size = ARRAY_SIZE(pvc_revid_step_tbl);
		basedie_info = pvc_basedie_subids;
		basedie_size = ARRAY_SIZE(pvc_basedie_subids);
	} else if (xe->info.subplatform == XE_SUBPLATFORM_DG2_G10) {
		revids = dg2_g10_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g10_revid_step_tbl);
	} else if (xe->info.subplatform == XE_SUBPLATFORM_DG2_G11) {
		revids = dg2_g11_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g11_revid_step_tbl);
	} else if (xe->info.subplatform == XE_SUBPLATFORM_DG2_G12) {
		revids = dg2_g12_revid_step_tbl;
		size = ARRAY_SIZE(dg2_g12_revid_step_tbl);
	} else if (xe->info.platform == XE_ALDERLAKE_S) {
		revids = adls_revids;
		size = ARRAY_SIZE(adls_revids);
	} else if (xe->info.platform == XE_DG1) {
		revids = dg1_revids;
		size = ARRAY_SIZE(dg1_revids);
	} else if (xe->info.platform == XE_TIGERLAKE) {
		revids = tgl_revids;
		size = ARRAY_SIZE(tgl_revids);
	}

	/* Not using the stepping scheme for the platform yet. */
	if (!revids)
		return step;

	if (revid < size && revids[revid].graphics != STEP_NONE) {
		step = revids[revid];
	} else {
		drm_warn(&xe->drm, "Unknown revid 0x%02x\n", revid);

		/*
		 * If we hit a gap in the revid array, use the information for
		 * the next revid.
		 *
		 * This may be wrong in all sorts of ways, especially if the
		 * steppings in the array are not monotonically increasing, but
		 * it's better than defaulting to 0.
		 */
		while (revid < size && revids[revid].graphics == STEP_NONE)
			revid++;

		if (revid < size) {
			drm_dbg(&xe->drm, "Using steppings for revid 0x%02x\n",
				revid);
			step = revids[revid];
		} else {
			drm_dbg(&xe->drm, "Using future steppings\n");
			step.graphics = STEP_FUTURE;
			step.display = STEP_FUTURE;
		}
	}

	drm_WARN_ON(&xe->drm, step.graphics == STEP_NONE);

	if (basedie_info && basedie_size) {
		if (baseid < basedie_size && basedie_info[baseid] != STEP_NONE) {
			step.basedie = basedie_info[baseid];
		} else {
			drm_warn(&xe->drm, "Unknown baseid 0x%02x\n", baseid);
			step.basedie = STEP_FUTURE;
		}
	}

	return step;
}

#define STEP_NAME_CASE(name)	\
	case STEP_##name:	\
		return #name;

const char *xe_step_name(enum xe_step step)
{
	switch (step) {
	STEP_NAME_LIST(STEP_NAME_CASE);

	default:
		return "**";
	}
}
