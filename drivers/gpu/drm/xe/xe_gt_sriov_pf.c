// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_pf_helpers.h"

/*
 * VF's metadata is maintained in the flexible array where:
 *   - entry [0] contains metadata for the PF (only if applicable),
 *   - entries [1..n] contain metadata for VF1..VFn::
 *
 *       <--------------------------- 1 + total_vfs ----------->
 *      +-------+-------+-------+-----------------------+-------+
 *      |   0   |   1   |   2   |                       |   n   |
 *      +-------+-------+-------+-----------------------+-------+
 *      |  PF   |  VF1  |  VF2  |      ...     ...      |  VFn  |
 *      +-------+-------+-------+-----------------------+-------+
 */
static int pf_alloc_metadata(struct xe_gt *gt)
{
	unsigned int num_vfs = xe_gt_sriov_pf_get_totalvfs(gt);

	gt->sriov.pf.vfs = drmm_kcalloc(&gt_to_xe(gt)->drm, 1 + num_vfs,
					sizeof(*gt->sriov.pf.vfs), GFP_KERNEL);
	if (!gt->sriov.pf.vfs)
		return -ENOMEM;

	return 0;
}

/**
 * xe_gt_sriov_pf_init_early - Prepare SR-IOV PF data structures on PF.
 * @gt: the &xe_gt to initialize
 *
 * Early initialization of the PF data.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_init_early(struct xe_gt *gt)
{
	int err;

	err = pf_alloc_metadata(gt);
	if (err)
		return err;

	return 0;
}
