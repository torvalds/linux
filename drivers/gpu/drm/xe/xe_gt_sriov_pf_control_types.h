/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_CONTROL_TYPES_H_
#define _XE_GT_SRIOV_PF_CONTROL_TYPES_H_

#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/workqueue_types.h>

/**
 * enum xe_gt_sriov_control_bits - Various bits used by the PF to represent a VF state
 *
 * @XE_GT_SRIOV_STATE_WIP: indicates that some operations are in progress.
 * @XE_GT_SRIOV_STATE_FLR_WIP: indicates that a VF FLR is in progress.
 * @XE_GT_SRIOV_STATE_FLR_SEND_START: indicates that the PF wants to send a FLR START command.
 * @XE_GT_SRIOV_STATE_FLR_WAIT_GUC: indicates that the PF awaits for a response from the GuC.
 * @XE_GT_SRIOV_STATE_FLR_GUC_DONE: indicates that the PF has received a response from the GuC.
 * @XE_GT_SRIOV_STATE_FLR_RESET_CONFIG: indicates that the PF needs to clear VF's resources.
 * @XE_GT_SRIOV_STATE_FLR_RESET_DATA: indicates that the PF needs to clear VF's data.
 * @XE_GT_SRIOV_STATE_FLR_RESET_MMIO: indicates that the PF needs to reset VF's registers.
 * @XE_GT_SRIOV_STATE_FLR_SEND_FINISH: indicates that the PF wants to send a FLR FINISH message.
 * @XE_GT_SRIOV_STATE_FLR_FAILED: indicates that VF FLR sequence failed.
 * @XE_GT_SRIOV_STATE_PAUSE_WIP: indicates that a VF pause operation is in progress.
 * @XE_GT_SRIOV_STATE_PAUSE_SEND_PAUSE: indicates that the PF is about to send a PAUSE command.
 * @XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC: indicates that the PF awaits for a response from the GuC.
 * @XE_GT_SRIOV_STATE_PAUSE_GUC_DONE: indicates that the PF has received a response from the GuC.
 * @XE_GT_SRIOV_STATE_PAUSE_FAILED: indicates that a VF pause operation has failed.
 * @XE_GT_SRIOV_STATE_PAUSED: indicates that the VF is paused.
 * @XE_GT_SRIOV_STATE_RESUME_WIP: indicates the a VF resume operation is in progress.
 * @XE_GT_SRIOV_STATE_RESUME_SEND_RESUME: indicates that the PF is about to send RESUME command.
 * @XE_GT_SRIOV_STATE_RESUME_FAILED: indicates that a VF resume operation has failed.
 * @XE_GT_SRIOV_STATE_RESUMED: indicates that the VF was resumed.
 * @XE_GT_SRIOV_STATE_STOP_WIP: indicates that a VF stop operation is in progress.
 * @XE_GT_SRIOV_STATE_STOP_SEND_STOP: indicates that the PF wants to send a STOP command.
 * @XE_GT_SRIOV_STATE_STOP_FAILED: indicates that the VF stop operation has failed
 * @XE_GT_SRIOV_STATE_STOPPED: indicates that the VF was stopped.
 * @XE_GT_SRIOV_STATE_MISMATCH: indicates that the PF has detected a VF state mismatch.
 */
enum xe_gt_sriov_control_bits {
	XE_GT_SRIOV_STATE_WIP = 1,

	XE_GT_SRIOV_STATE_FLR_WIP,
	XE_GT_SRIOV_STATE_FLR_SEND_START,
	XE_GT_SRIOV_STATE_FLR_WAIT_GUC,
	XE_GT_SRIOV_STATE_FLR_GUC_DONE,
	XE_GT_SRIOV_STATE_FLR_RESET_CONFIG,
	XE_GT_SRIOV_STATE_FLR_RESET_DATA,
	XE_GT_SRIOV_STATE_FLR_RESET_MMIO,
	XE_GT_SRIOV_STATE_FLR_SEND_FINISH,
	XE_GT_SRIOV_STATE_FLR_FAILED,

	XE_GT_SRIOV_STATE_PAUSE_WIP,
	XE_GT_SRIOV_STATE_PAUSE_SEND_PAUSE,
	XE_GT_SRIOV_STATE_PAUSE_WAIT_GUC,
	XE_GT_SRIOV_STATE_PAUSE_GUC_DONE,
	XE_GT_SRIOV_STATE_PAUSE_FAILED,
	XE_GT_SRIOV_STATE_PAUSED,

	XE_GT_SRIOV_STATE_RESUME_WIP,
	XE_GT_SRIOV_STATE_RESUME_SEND_RESUME,
	XE_GT_SRIOV_STATE_RESUME_FAILED,
	XE_GT_SRIOV_STATE_RESUMED,

	XE_GT_SRIOV_STATE_STOP_WIP,
	XE_GT_SRIOV_STATE_STOP_SEND_STOP,
	XE_GT_SRIOV_STATE_STOP_FAILED,
	XE_GT_SRIOV_STATE_STOPPED,

	XE_GT_SRIOV_STATE_MISMATCH = BITS_PER_LONG - 1,
};

/**
 * struct xe_gt_sriov_control_state - GT-level per-VF control state.
 *
 * Used by the PF driver to maintain per-VF control data.
 */
struct xe_gt_sriov_control_state {
	/** @state: VF state bits */
	unsigned long state;

	/** @done: completion of async operations */
	struct completion done;

	/** @link: link into worker list */
	struct list_head link;
};

/**
 * struct xe_gt_sriov_pf_control - GT-level control data.
 *
 * Used by the PF driver to maintain its data.
 */
struct xe_gt_sriov_pf_control {
	/** @worker: worker that executes a VF operations */
	struct work_struct worker;

	/** @list: list of VF entries that have a pending work */
	struct list_head list;

	/** @lock: protects VF pending list */
	spinlock_t lock;
};

#endif
