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
struct xe_uc_fw_version;

int xe_gt_sriov_vf_reset(struct xe_gt *gt);
int xe_gt_sriov_vf_bootstrap(struct xe_gt *gt);
void xe_gt_sriov_vf_guc_versions(struct xe_gt *gt,
				 struct xe_uc_fw_version *wanted,
				 struct xe_uc_fw_version *found);
int xe_gt_sriov_vf_query_config(struct xe_gt *gt);
int xe_gt_sriov_vf_connect(struct xe_gt *gt);
int xe_gt_sriov_vf_query_runtime(struct xe_gt *gt);
void xe_gt_sriov_vf_default_lrcs_hwsp_rebase(struct xe_gt *gt);
int xe_gt_sriov_vf_notify_resfix_done(struct xe_gt *gt);
void xe_gt_sriov_vf_migrated_event_handler(struct xe_gt *gt);

u32 xe_gt_sriov_vf_gmdid(struct xe_gt *gt);
u16 xe_gt_sriov_vf_guc_ids(struct xe_gt *gt);
u64 xe_gt_sriov_vf_lmem(struct xe_gt *gt);
u64 xe_gt_sriov_vf_ggtt(struct xe_gt *gt);
u64 xe_gt_sriov_vf_ggtt_base(struct xe_gt *gt);
s64 xe_gt_sriov_vf_ggtt_shift(struct xe_gt *gt);

u32 xe_gt_sriov_vf_read32(struct xe_gt *gt, struct xe_reg reg);
void xe_gt_sriov_vf_write32(struct xe_gt *gt, struct xe_reg reg, u32 val);

void xe_gt_sriov_vf_print_config(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_sriov_vf_print_runtime(struct xe_gt *gt, struct drm_printer *p);
void xe_gt_sriov_vf_print_version(struct xe_gt *gt, struct drm_printer *p);

#endif
