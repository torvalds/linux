// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>
#include <drm/xe_drm.h>

#include "regs/xe_oa_regs.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_oa.h"

#define XE_OA_UNIT_INVALID U32_MAX

#define DRM_FMT(x) DRM_XE_OA_FMT_TYPE_##x

static const struct xe_oa_format oa_formats[] = {
	[XE_OA_FORMAT_C4_B8]			= { 7, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12]			= { 0, 64,  DRM_FMT(OAG) },
	[XE_OA_FORMAT_A12_B8_C8]		= { 2, 128, DRM_FMT(OAG) },
	[XE_OA_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAR_FORMAT_A32u40_A4u32_B8_C8]	= { 5, 256, DRM_FMT(OAR) },
	[XE_OA_FORMAT_A24u40_A14u32_B8_C8]	= { 5, 256, DRM_FMT(OAG) },
	[XE_OAC_FORMAT_A24u64_B8_C8]		= { 1, 320, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAC_FORMAT_A22u32_R2u32_B8_C8]	= { 2, 192, DRM_FMT(OAC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u64_B8_C8]		= { 1, 192, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OAM_FORMAT_MPEC8u32_B8_C8]		= { 2, 128, DRM_FMT(OAM_MPEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC64u64]			= { 1, 576, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC64u64_B8_C8]		= { 1, 640, DRM_FMT(PEC), HDR_64_BIT, 1, 1 },
	[XE_OA_FORMAT_PEC64u32]			= { 1, 320, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G1]		= { 5, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G1]		= { 5, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC32u64_G2]		= { 6, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC32u32_G2]		= { 6, 192, DRM_FMT(PEC), HDR_64_BIT },
	[XE_OA_FORMAT_PEC36u64_G1_32_G2_4]	= { 3, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
	[XE_OA_FORMAT_PEC36u64_G1_4_G2_32]	= { 4, 320, DRM_FMT(PEC), HDR_64_BIT, 1, 0 },
};

static u32 num_oa_units_per_gt(struct xe_gt *gt)
{
	return 1;
}

static u32 __hwe_oam_unit(struct xe_hw_engine *hwe)
{
	if (GRAPHICS_VERx100(gt_to_xe(hwe->gt)) >= 1270) {
		/*
		 * There's 1 SAMEDIA gt and 1 OAM per SAMEDIA gt. All media slices
		 * within the gt use the same OAM. All MTL/LNL SKUs list 1 SA MEDIA
		 */
		xe_gt_WARN_ON(hwe->gt, hwe->gt->info.type != XE_GT_TYPE_MEDIA);

		return 0;
	}

	return XE_OA_UNIT_INVALID;
}

static u32 __hwe_oa_unit(struct xe_hw_engine *hwe)
{
	switch (hwe->class) {
	case XE_ENGINE_CLASS_RENDER:
	case XE_ENGINE_CLASS_COMPUTE:
		return 0;

	case XE_ENGINE_CLASS_VIDEO_DECODE:
	case XE_ENGINE_CLASS_VIDEO_ENHANCE:
		return __hwe_oam_unit(hwe);

	default:
		return XE_OA_UNIT_INVALID;
	}
}

static struct xe_oa_regs __oam_regs(u32 base)
{
	return (struct xe_oa_regs) {
		base,
		OAM_HEAD_POINTER(base),
		OAM_TAIL_POINTER(base),
		OAM_BUFFER(base),
		OAM_CONTEXT_CONTROL(base),
		OAM_CONTROL(base),
		OAM_DEBUG(base),
		OAM_STATUS(base),
		OAM_CONTROL_COUNTER_SEL_MASK,
	};
}

static struct xe_oa_regs __oag_regs(void)
{
	return (struct xe_oa_regs) {
		0,
		OAG_OAHEADPTR,
		OAG_OATAILPTR,
		OAG_OABUFFER,
		OAG_OAGLBCTXCTRL,
		OAG_OACONTROL,
		OAG_OA_DEBUG,
		OAG_OASTATUS,
		OAG_OACONTROL_OA_COUNTER_SEL_MASK,
	};
}

static void __xe_oa_init_oa_units(struct xe_gt *gt)
{
	const u32 mtl_oa_base[] = { 0x13000 };
	int i, num_units = gt->oa.num_oa_units;

	for (i = 0; i < num_units; i++) {
		struct xe_oa_unit *u = &gt->oa.oa_unit[i];

		if (gt->info.type != XE_GT_TYPE_MEDIA) {
			u->regs = __oag_regs();
			u->type = DRM_XE_OA_UNIT_TYPE_OAG;
		} else if (GRAPHICS_VERx100(gt_to_xe(gt)) >= 1270) {
			u->regs = __oam_regs(mtl_oa_base[i]);
			u->type = DRM_XE_OA_UNIT_TYPE_OAM;
		}

		/* Set oa_unit_ids now to ensure ids remain contiguous */
		u->oa_unit_id = gt_to_xe(gt)->oa.oa_unit_ids++;
	}
}

static int xe_oa_init_gt(struct xe_gt *gt)
{
	u32 num_oa_units = num_oa_units_per_gt(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_oa_unit *u;

	u = drmm_kcalloc(&gt_to_xe(gt)->drm, num_oa_units, sizeof(*u), GFP_KERNEL);
	if (!u)
		return -ENOMEM;

	for_each_hw_engine(hwe, gt, id) {
		u32 index = __hwe_oa_unit(hwe);

		hwe->oa_unit = NULL;
		if (index < num_oa_units) {
			u[index].num_engines++;
			hwe->oa_unit = &u[index];
		}
	}

	/*
	 * Fused off engines can result in oa_unit's with num_engines == 0. These units
	 * will appear in OA unit query, but no perf streams can be opened on them.
	 */
	gt->oa.num_oa_units = num_oa_units;
	gt->oa.oa_unit = u;

	__xe_oa_init_oa_units(gt);

	drmm_mutex_init(&gt_to_xe(gt)->drm, &gt->oa.gt_lock);

	return 0;
}

static int xe_oa_init_oa_units(struct xe_oa *oa)
{
	struct xe_gt *gt;
	int i, ret;

	for_each_gt(gt, oa->xe, i) {
		ret = xe_oa_init_gt(gt);
		if (ret)
			return ret;
	}

	return 0;
}

static void oa_format_add(struct xe_oa *oa, enum xe_oa_format_name format)
{
	__set_bit(format, oa->format_mask);
}

static void xe_oa_init_supported_formats(struct xe_oa *oa)
{
	if (GRAPHICS_VER(oa->xe) >= 20) {
		/* Xe2+ */
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u64_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_PEC64u32);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G1);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u64_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC32u32_G2);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_32_G2_4);
		oa_format_add(oa, XE_OA_FORMAT_PEC36u64_G1_4_G2_32);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1270) {
		/* XE_METEORLAKE */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u64_B8_C8);
		oa_format_add(oa, XE_OAM_FORMAT_MPEC8u32_B8_C8);
	} else if (GRAPHICS_VERx100(oa->xe) >= 1255) {
		/* XE_DG2, XE_PVC */
		oa_format_add(oa, XE_OAR_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A24u40_A14u32_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A24u64_B8_C8);
		oa_format_add(oa, XE_OAC_FORMAT_A22u32_R2u32_B8_C8);
	} else {
		/* Gen12+ */
		xe_assert(oa->xe, GRAPHICS_VER(oa->xe) >= 12);
		oa_format_add(oa, XE_OA_FORMAT_A12);
		oa_format_add(oa, XE_OA_FORMAT_A12_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_A32u40_A4u32_B8_C8);
		oa_format_add(oa, XE_OA_FORMAT_C4_B8);
	}
}

/**
 * xe_oa_init - OA initialization during device probe
 * @xe: @xe_device
 *
 * Return: 0 on success or a negative error code on failure
 */
int xe_oa_init(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;
	int ret;

	/* Support OA only with GuC submission and Gen12+ */
	if (XE_WARN_ON(!xe_device_uc_enabled(xe)) || XE_WARN_ON(GRAPHICS_VER(xe) < 12))
		return 0;

	oa->xe = xe;
	oa->oa_formats = oa_formats;

	ret = xe_oa_init_oa_units(oa);
	if (ret) {
		drm_err(&xe->drm, "OA initialization failed (%pe)\n", ERR_PTR(ret));
		goto exit;
	}

	xe_oa_init_supported_formats(oa);
	return 0;
exit:
	oa->xe = NULL;
	return ret;
}

/**
 * xe_oa_fini - OA de-initialization during device remove
 * @xe: @xe_device
 */
void xe_oa_fini(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	if (!oa->xe)
		return;

	oa->xe = NULL;
}
