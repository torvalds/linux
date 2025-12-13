/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2025 Intel Corporation
 */

#ifndef _XE_SRIOV_VF_TYPES_H_
#define _XE_SRIOV_VF_TYPES_H_

#include <linux/types.h>
#include <linux/workqueue_types.h>

#include "xe_sriov_vf_ccs_types.h"

/**
 * struct xe_sriov_vf_relay_version - PF ABI version details.
 */
struct xe_sriov_vf_relay_version {
	/** @major: major version. */
	u16 major;
	/** @minor: minor version. */
	u16 minor;
};

/**
 * struct xe_device_vf - Xe Virtual Function related data
 *
 * The data in this structure is valid only if driver is running in the
 * @XE_SRIOV_MODE_VF mode.
 */
struct xe_device_vf {
	/** @pf_version: negotiated VF/PF ABI version. */
	struct xe_sriov_vf_relay_version pf_version;

	/** @migration: VF Migration state data */
	struct {
		/** @migration.worker: VF migration recovery worker */
		struct work_struct worker;
		/** @migration.gt_flags: Per-GT request flags for VF migration recovery */
		unsigned long gt_flags;
		/**
		 * @migration.enabled: flag indicating if migration support
		 * was enabled or not due to missing prerequisites
		 */
		bool enabled;
	} migration;

	/** @ccs: VF CCS state data */
	struct xe_sriov_vf_ccs ccs;
};

#endif
