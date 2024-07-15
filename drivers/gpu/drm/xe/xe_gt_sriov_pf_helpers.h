/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_HELPERS_H_
#define _XE_GT_SRIOV_PF_HELPERS_H_

#include "xe_gt_types.h"
#include "xe_sriov_pf_helpers.h"

/**
 * xe_gt_sriov_pf_assert_vfid() - warn if &id is not a supported VF number when debugging.
 * @gt: the PF &xe_gt to assert on
 * @vfid: the VF number to assert
 *
 * Assert that &gt belongs to the Physical Function (PF) device and provided &vfid
 * is within a range of supported VF numbers (up to maximum number of VFs that
 * driver can support, including VF0 that represents the PF itself).
 *
 * Note: Effective only on debug builds. See `Xe ASSERTs`_ for more information.
 */
#define xe_gt_sriov_pf_assert_vfid(gt, vfid)	xe_sriov_pf_assert_vfid(gt_to_xe(gt), (vfid))

static inline int xe_gt_sriov_pf_get_totalvfs(struct xe_gt *gt)
{
	return xe_sriov_pf_get_totalvfs(gt_to_xe(gt));
}

static inline struct mutex *xe_gt_sriov_pf_master_mutex(struct xe_gt *gt)
{
	return xe_sriov_pf_master_mutex(gt_to_xe(gt));
}

#endif
