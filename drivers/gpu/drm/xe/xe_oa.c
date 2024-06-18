// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/xe_drm.h>

#include "xe_assert.h"
#include "xe_device.h"
#include "xe_macros.h"
#include "xe_oa.h"

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

	/* Support OA only with GuC submission and Gen12+ */
	if (XE_WARN_ON(!xe_device_uc_enabled(xe)) || XE_WARN_ON(GRAPHICS_VER(xe) < 12))
		return 0;

	oa->xe = xe;
	oa->oa_formats = oa_formats;

	xe_oa_init_supported_formats(oa);
	return 0;
}

/**
 * xe_oa_fini - OA de-initialization during device remove
 * @xe: @xe_device
 */
void xe_oa_fini(struct xe_device *xe)
{
	struct xe_oa *oa = &xe->oa;

	oa->xe = NULL;
}
