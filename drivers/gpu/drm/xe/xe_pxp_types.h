/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2024, Intel Corporation. All rights reserved.
 */

#ifndef __XE_PXP_TYPES_H__
#define __XE_PXP_TYPES_H__

struct xe_device;
struct xe_gt;

/**
 * struct xe_pxp - pxp state
 */
struct xe_pxp {
	/** @xe: Backpoiner to the xe_device struct */
	struct xe_device *xe;

	/**
	 * @gt: pointer to the gt that owns the submission-side of PXP
	 * (VDBOX, KCR and GSC)
	 */
	struct xe_gt *gt;
};

#endif /* __XE_PXP_TYPES_H__ */
