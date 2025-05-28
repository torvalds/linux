/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_GUC_ENGINE_ACTIVITY_TYPES_H_
#define _XE_GUC_ENGINE_ACTIVITY_TYPES_H_

#include <linux/types.h>

#include "xe_guc_fwif.h"
/**
 * struct engine_activity - Engine specific activity data
 *
 * Contains engine specific activity data and snapshot of the
 * structures from GuC
 */
struct engine_activity {
	/** @active: current activity */
	u64 active;

	/** @last_cpu_ts: cpu timestamp in nsec of previous sample */
	u64 last_cpu_ts;

	/** @quanta: total quanta used on HW */
	u64 quanta;

	/** @quanta_ns: total quanta_ns used on HW */
	u64 quanta_ns;

	/**
	 * @quanta_remainder_ns: remainder when the CPU time is scaled as
	 * per the quanta_ratio. This remainder is used in subsequent
	 * quanta calculations.
	 */
	u64 quanta_remainder_ns;

	/** @total: total engine activity */
	u64 total;

	/** @running: true if engine is running some work */
	bool running;

	/** @metadata: snapshot of engine activity metadata */
	struct guc_engine_activity_metadata metadata;

	/** @activity: snapshot of engine activity counter */
	struct guc_engine_activity activity;
};

/**
 * struct engine_activity_group - Activity data for all engines
 */
struct engine_activity_group {
	/** @engine: engine specific activity data */
	struct engine_activity engine[GUC_MAX_ENGINE_CLASSES][GUC_MAX_INSTANCES_PER_CLASS];
};

/**
 * struct engine_activity_buffer - engine activity buffers
 *
 * This contains the buffers allocated for metadata and activity data
 */
struct engine_activity_buffer {
	/** @activity_bo: object allocated to hold activity data */
	struct xe_bo *activity_bo;

	/** @metadata_bo: object allocated to hold activity metadata */
	struct xe_bo *metadata_bo;
};

/**
 * struct xe_guc_engine_activity - Data used by engine activity implementation
 */
struct xe_guc_engine_activity {
	/** @gpm_timestamp_shift: Right shift value for the gpm timestamp */
	u32 gpm_timestamp_shift;

	/** @num_activity_group: number of activity groups */
	u32 num_activity_group;

	/** @num_functions: number of functions */
	u32 num_functions;

	/** @supported: indicates support for engine activity stats */
	bool supported;

	/**
	 * @eag: holds the device level engine activity data in native mode.
	 * In SRIOV mode, points to an array with entries which holds the engine
	 * activity data for PF and VF's
	 */
	struct engine_activity_group *eag;

	/** @device_buffer: buffer object for global engine activity */
	struct engine_activity_buffer device_buffer;

	/** @function_buffer: buffer object for per-function engine activity */
	struct engine_activity_buffer function_buffer;
};
#endif

