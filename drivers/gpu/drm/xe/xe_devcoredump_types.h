/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_DEVCOREDUMP_TYPES_H_
#define _XE_DEVCOREDUMP_TYPES_H_

#include <linux/ktime.h>
#include <linux/mutex.h>

#include "xe_hw_engine_types.h"

struct xe_device;
struct xe_gt;

/**
 * struct xe_devcoredump_snapshot - Crash snapshot
 *
 * This struct contains all the useful information quickly captured at the time
 * of the crash. So, any subsequent reads of the coredump points to a data that
 * shows the state of the GPU of when the issue has happened.
 */
struct xe_devcoredump_snapshot {
	/** @snapshot_time:  Time of this capture. */
	ktime_t snapshot_time;
	/** @boot_time:  Relative boot time so the uptime can be calculated. */
	ktime_t boot_time;
	/** @process_name: Name of process that triggered this gpu hang */
	char process_name[TASK_COMM_LEN];

	/** @gt: Affected GT, used by forcewake for delayed capture */
	struct xe_gt *gt;
	/** @work: Workqueue for deferred capture outside of signaling context */
	struct work_struct work;

	/* GuC snapshots */
	/** @ct: GuC CT snapshot */
	struct xe_guc_ct_snapshot *ct;
	/** @ge: Guc Engine snapshot */
	struct xe_guc_submit_exec_queue_snapshot *ge;

	/** @hwe: HW Engine snapshot array */
	struct xe_hw_engine_snapshot *hwe[XE_NUM_HW_ENGINES];
	/** @job: Snapshot of job state */
	struct xe_sched_job_snapshot *job;
	/** @vm: Snapshot of VM state */
	struct xe_vm_snapshot *vm;
};

/**
 * struct xe_devcoredump - Xe devcoredump main structure
 *
 * This struct represents the live and active dev_coredump node.
 * It is created/populated at the time of a crash/error. Then it
 * is read later when user access the device coredump data file
 * for reading the information.
 */
struct xe_devcoredump {
	/** @captured: The snapshot of the first hang has already been taken. */
	bool captured;
	/** @snapshot: Snapshot is captured at time of the first crash */
	struct xe_devcoredump_snapshot snapshot;
};

#endif
