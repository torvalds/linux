/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_MIGRATION_TYPES_H_
#define _XE_SRIOV_PF_MIGRATION_TYPES_H_

#include <linux/types.h>
#include <linux/wait.h>

/**
 * struct xe_sriov_pf_migration - Xe device level VF migration data
 */
struct xe_sriov_pf_migration {
	/** @supported: indicates whether VF migration feature is supported */
	bool supported;
};

/**
 * struct xe_sriov_migration_state - Per VF device-level migration related data
 */
struct xe_sriov_migration_state {
	/** @wq: waitqueue used to avoid busy-waiting for snapshot production/consumption */
	wait_queue_head_t wq;
};

#endif
