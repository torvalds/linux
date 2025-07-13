/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_TYPES_H_
#define _XE_SRIOV_VF_TYPES_H_

#include <linux/workqueue_types.h>

/**
 * struct xe_device_vf - Xe Virtual Function related data
 *
 * The data in this structure is valid only if driver is running in the
 * @XE_SRIOV_MODE_VF mode.
 */
struct xe_device_vf {
	/** @migration: VF Migration state data */
	struct {
		/** @migration.worker: VF migration recovery worker */
		struct work_struct worker;
		/** @migration.gt_flags: Per-GT request flags for VF migration recovery */
		unsigned long gt_flags;
	} migration;
};

#endif
