/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SRIOV_PF_MIGRATION_TYPES_H_
#define _XE_SRIOV_PF_MIGRATION_TYPES_H_

#include <linux/types.h>
#include <linux/mutex_types.h>
#include <linux/wait.h>

/**
 * struct xe_sriov_pf_migration - Xe device level VF migration data
 */
struct xe_sriov_pf_migration {
	/** @disabled: indicates whether VF migration feature is disabled */
	bool disabled;
};

/**
 * struct xe_sriov_migration_state - Per VF device-level migration related data
 */
struct xe_sriov_migration_state {
	/** @wq: waitqueue used to avoid busy-waiting for snapshot production/consumption */
	wait_queue_head_t wq;
	/** @lock: Mutex protecting the migration data */
	struct mutex lock;
	/** @pending: currently processed data packet of VF resource */
	struct xe_sriov_packet *pending;
	/** @trailer: data packet used to indicate the end of stream */
	struct xe_sriov_packet *trailer;
	/** @descriptor: data packet containing the metadata describing the device */
	struct xe_sriov_packet *descriptor;
};

#endif
