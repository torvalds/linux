/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_CONFIG_TYPES_H_
#define _XE_GT_SRIOV_PF_CONFIG_TYPES_H_

#include <drm/drm_mm.h>

#include "xe_guc_klv_thresholds_set_types.h"

struct xe_bo;

/**
 * struct xe_gt_sriov_config - GT level per-VF configuration data.
 *
 * Used by the PF driver to maintain per-VF provisioning data.
 */
struct xe_gt_sriov_config {
	/** @ggtt_region: GGTT region assigned to the VF. */
	struct drm_mm_node ggtt_region;
	/** @lmem_obj: LMEM allocation for use by the VF. */
	struct xe_bo *lmem_obj;
	/** @num_ctxs: number of GuC contexts IDs.  */
	u16 num_ctxs;
	/** @begin_ctx: start index of GuC context ID range. */
	u16 begin_ctx;
	/** @num_dbs: number of GuC doorbells IDs. */
	u16 num_dbs;
	/** @begin_db: start index of GuC doorbell ID range. */
	u16 begin_db;
	/** @exec_quantum: execution-quantum in milliseconds. */
	u32 exec_quantum;
	/** @preempt_timeout: preemption timeout in microseconds. */
	u32 preempt_timeout;
	/** @thresholds: GuC thresholds for adverse events notifications. */
	u32 thresholds[XE_GUC_KLV_NUM_THRESHOLDS];
};

/**
 * struct xe_gt_sriov_spare_config - GT-level PF spare configuration data.
 *
 * Used by the PF driver to maintain it's own reserved (spare) provisioning
 * data that is not applicable to be tracked in struct xe_gt_sriov_config.
 */
struct xe_gt_sriov_spare_config {
	/** @ggtt_size: GGTT size. */
	u64 ggtt_size;
	/** @lmem_size: LMEM size. */
	u64 lmem_size;
	/** @num_ctxs: number of GuC submission contexts. */
	u16 num_ctxs;
	/** @num_dbs: number of GuC doorbells. */
	u16 num_dbs;
};

#endif
