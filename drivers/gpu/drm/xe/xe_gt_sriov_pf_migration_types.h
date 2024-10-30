/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MIGRATION_TYPES_H_
#define _XE_GT_SRIOV_PF_MIGRATION_TYPES_H_

#include <linux/mutex.h>
#include <linux/types.h>

/**
 * struct xe_gt_sriov_state_snapshot - GT-level per-VF state snapshot data.
 *
 * Used by the PF driver to maintain per-VF migration data.
 */
struct xe_gt_sriov_state_snapshot {
	/** @guc: GuC VF state snapshot */
	struct {
		/** @guc.buff: buffer with the VF state */
		u32 *buff;
		/** @guc.size: size of the buffer (must be dwords aligned) */
		u32 size;
	} guc;
};

/**
 * struct xe_gt_sriov_pf_migration - GT-level data.
 *
 * Used by the PF driver to maintain non-VF specific per-GT data.
 */
struct xe_gt_sriov_pf_migration {
	/** @supported: indicates whether the feature is supported */
	bool supported;

	/** @snapshot_lock: protects all VFs snapshots */
	struct mutex snapshot_lock;
};

#endif
