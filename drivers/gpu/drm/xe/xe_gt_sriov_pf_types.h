/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_TYPES_H_
#define _XE_GT_SRIOV_PF_TYPES_H_

#include <linux/types.h>

#include "xe_gt_sriov_pf_config_types.h"
#include "xe_gt_sriov_pf_control_types.h"
#include "xe_gt_sriov_pf_monitor_types.h"
#include "xe_gt_sriov_pf_policy_types.h"
#include "xe_gt_sriov_pf_service_types.h"

/**
 * struct xe_gt_sriov_metadata - GT level per-VF metadata.
 */
struct xe_gt_sriov_metadata {
	/** @config: per-VF provisioning data. */
	struct xe_gt_sriov_config config;

	/** @monitor: per-VF monitoring data. */
	struct xe_gt_sriov_monitor monitor;

	/** @control: per-VF control data. */
	struct xe_gt_sriov_control_state control;

	/** @version: negotiated VF/PF ABI version */
	struct xe_gt_sriov_pf_service_version version;
};

/**
 * struct xe_gt_sriov_pf - GT level PF virtualization data.
 * @service: service data.
 * @control: control data.
 * @policy: policy data.
 * @spare: PF-only provisioning configuration.
 * @vfs: metadata for all VFs.
 */
struct xe_gt_sriov_pf {
	struct xe_gt_sriov_pf_service service;
	struct xe_gt_sriov_pf_control control;
	struct xe_gt_sriov_pf_policy policy;
	struct xe_gt_sriov_spare_config spare;
	struct xe_gt_sriov_metadata *vfs;
};

#endif
