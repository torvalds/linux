/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_PLATFORM_INFO_TYPES_H_
#define _XE_PLATFORM_INFO_TYPES_H_

/* Keep in gen based order, and chronological order within a gen */
enum xe_platform {
	XE_PLATFORM_UNINITIALIZED = 0,
	/* gen12 */
	XE_TIGERLAKE,
	XE_ROCKETLAKE,
	XE_DG1,
	XE_DG2,
	XE_PVC,
	XE_ALDERLAKE_S,
	XE_ALDERLAKE_P,
	XE_METEORLAKE,
};

enum xe_subplatform {
	XE_SUBPLATFORM_UNINITIALIZED = 0,
	XE_SUBPLATFORM_NONE,
	XE_SUBPLATFORM_DG2_G10,
	XE_SUBPLATFORM_DG2_G11,
	XE_SUBPLATFORM_DG2_G12,
	XE_SUBPLATFORM_ADLP_RPLU,
};

#endif
