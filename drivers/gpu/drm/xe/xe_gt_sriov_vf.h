/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_VF_H_
#define _XE_GT_SRIOV_VF_H_

#include <linux/types.h>

struct drm_printer;
struct xe_gt;
struct xe_reg;

int xe_gt_sriov_vf_bootstrap(struct xe_gt *gt);
int xe_gt_sriov_vf_query_config(struct xe_gt *gt);
int xe_gt_sriov_vf_connect(struct xe_gt *gt);
int xe_gt_sriov_vf_query_runtime(struct xe_gt *gt);
int xe_gt_sriov_vf_prepare_ggtt(struct xe_gt *gt);

u32 xe_gt_sriov_vf_gmdid(struct xe_gt *gt);
u16 xe_gt_sriov_vf_guc_ids(struct xe_gt *gt);
u32 xe_gt_sriov_vf_read32(struct xe_gt *gt, struct xe_reg reg);

void xe_gt_sriov_vf_print_config(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_sriov_vf_print_runtime(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_sriov_vf_print_version(struct xe_gt *gt, struct drm_printer *p);

#endif
