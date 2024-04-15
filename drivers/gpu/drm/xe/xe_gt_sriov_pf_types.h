/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_TYPES_H_
#define _XE_GT_SRIOV_PF_TYPES_H_

#include <linux/types.h>

#include "xe_gt_sriov_pf_policy_types.h"

/**
 * struct xe_gt_sriov_metadata - GT level per-VF metadata.
 */
struct xe_gt_sriov_metadata {
	/* XXX: VF metadata will go here */
};

/**
 * struct xe_gt_sriov_pf - GT level PF virtualization data.
 * @policy: policy data.
 * @vfs: metadata for all VFs.
 */
struct xe_gt_sriov_pf {
	struct xe_gt_sriov_pf_policy policy;
	struct xe_gt_sriov_metadata *vfs;
};

#endif
