/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MONITOR_TYPES_H_
#define _XE_GT_SRIOV_PF_MONITOR_TYPES_H_

#include "xe_guc_klv_thresholds_set_types.h"

/**
 * struct xe_gt_sriov_monitor - GT level per-VF monitoring data.
 */
struct xe_gt_sriov_monitor {
	/** @guc: monitoring data related to the GuC. */
	struct {
		/** @guc.events: number of adverse events reported by the GuC. */
		unsigned int events[XE_GUC_KLV_NUM_THRESHOLDS];
	} guc;
};

#endif
