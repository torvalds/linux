/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_OA_TYPES_H_
#define _XE_OA_TYPES_H_

#include <linux/bitops.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <uapi/drm/xe_drm.h>
#include "regs/xe_reg_defs.h"
#include "xe_hw_engine_types.h"

#define DEFAULT_XE_OA_BUFFER_SIZE SZ_16M

enum xe_oa_report_header {
	HDR_32_BIT = 0,
	HDR_64_BIT,
};

enum xe_oa_format_name {
	XE_OA_FORMAT_C4_B8,

	/* Gen8+ */
	XE_OA_FORMAT_A12,
	XE_OA_FORMAT_A12_B8_C8,
	XE_OA_FORMAT_A32u40_A4u32_B8_C8,

	/* DG2 */
	XE_OAR_FORMAT_A32u40_A4u32_B8_C8,
	XE_OA_FORMAT_A24u40_A14u32_B8_C8,

	/* DG2/MTL OAC */
	XE_OAC_FORMAT_A24u64_B8_C8,
	XE_OAC_FORMAT_A22u32_R2u32_B8_C8,

	/* MTL OAM */
	XE_OAM_FORMAT_MPEC8u64_B8_C8,
	XE_OAM_FORMAT_MPEC8u32_B8_C8,

	/* Xe2+ */
	XE_OA_FORMAT_PEC64u64,
	XE_OA_FORMAT_PEC64u64_B8_C8,
	XE_OA_FORMAT_PEC64u32,
	XE_OA_FORMAT_PEC32u64_G1,
	XE_OA_FORMAT_PEC32u32_G1,
	XE_OA_FORMAT_PEC32u64_G2,
	XE_OA_FORMAT_PEC32u32_G2,
	XE_OA_FORMAT_PEC36u64_G1_32_G2_4,
	XE_OA_FORMAT_PEC36u64_G1_4_G2_32,

	__XE_OA_FORMAT_MAX,
};

/**
 * struct xe_oa_format - Format fields for supported OA formats. OA format
 * properties are specified in PRM/Bspec 52198 and 60942
 */
struct xe_oa_format {
	/** @counter_select: counter select value (see Bspec 52198/60942) */
	u32 counter_select;
	/** @size: record size as written by HW (multiple of 64 byte cachelines) */
	int size;
	/** @type: of enum @drm_xe_oa_format_type */
	int type;
	/** @header: 32 or 64 bit report headers */
	enum xe_oa_report_header header;
	/** @counter_size: counter size value (see Bspec 60942) */
	u16 counter_size;
	/** @bc_report: BC report value (see Bspec 60942) */
	u16 bc_report;
};

/** struct xe_oa_regs - Registers for each OA unit */
struct xe_oa_regs {
	u32 base;
	struct xe_reg oa_head_ptr;
	struct xe_reg oa_tail_ptr;
	struct xe_reg oa_buffer;
	struct xe_reg oa_ctx_ctrl;
	struct xe_reg oa_ctrl;
	struct xe_reg oa_debug;
	struct xe_reg oa_status;
	u32 oa_ctrl_counter_select_mask;
};

/**
 * struct xe_oa_unit - Hardware OA unit
 */
struct xe_oa_unit {
	/** @oa_unit_id: identifier for the OA unit */
	u16 oa_unit_id;

	/** @type: Type of OA unit - OAM, OAG etc. */
	enum drm_xe_oa_unit_type type;

	/** @regs: OA registers for programming the OA unit */
	struct xe_oa_regs regs;

	/** @num_engines: number of engines attached to this OA unit */
	u32 num_engines;

	/** @exclusive_stream: The stream currently using the OA unit */
	struct xe_oa_stream *exclusive_stream;
};

/**
 * struct xe_oa_gt - OA per-gt information
 */
struct xe_oa_gt {
	/** @gt_lock: lock protecting create/destroy OA streams */
	struct mutex gt_lock;

	/** @num_oa_units: number of oa units for each gt */
	u32 num_oa_units;

	/** @oa_unit: array of oa_units */
	struct xe_oa_unit *oa_unit;
};

/**
 * struct xe_oa - OA device level information
 */
struct xe_oa {
	/** @xe: back pointer to xe device */
	struct xe_device *xe;

	/** @metrics_kobj: kobj for metrics sysfs */
	struct kobject *metrics_kobj;

	/** @metrics_lock: lock protecting add/remove configs */
	struct mutex metrics_lock;

	/** @metrics_idr: List of dynamic configurations (struct xe_oa_config) */
	struct idr metrics_idr;

	/** @oa_formats: tracks all OA formats across platforms */
	const struct xe_oa_format *oa_formats;

	/** @format_mask: tracks valid OA formats for a platform */
	unsigned long format_mask[BITS_TO_LONGS(__XE_OA_FORMAT_MAX)];

	/** @oa_unit_ids: tracks oa unit ids assigned across gt's */
	u16 oa_unit_ids;
};

/** @xe_oa_buffer: State of the stream OA buffer */
struct xe_oa_buffer {
	/** @format: data format */
	const struct xe_oa_format *format;

	/** @format: xe_bo backing the OA buffer */
	struct xe_bo *bo;

	/** @vaddr: mapped vaddr of the OA buffer */
	u8 *vaddr;

	/** @ptr_lock: Lock protecting reads/writes to head/tail pointers */
	spinlock_t ptr_lock;

	/** @head: Cached head to read from */
	u32 head;

	/** @tail: The last verified cached tail where HW has completed writing */
	u32 tail;

	/** @circ_size: The effective circular buffer size, for Xe2+ */
	u32 circ_size;
};

/**
 * struct xe_oa_stream - state for a single open stream FD
 */
struct xe_oa_stream {
	/** @oa: xe_oa backpointer */
	struct xe_oa *oa;

	/** @gt: gt associated with the oa stream */
	struct xe_gt *gt;

	/** @hwe: hardware engine associated with this oa stream */
	struct xe_hw_engine *hwe;

	/** @stream_lock: Lock serializing stream operations */
	struct mutex stream_lock;

	/** @sample: true if DRM_XE_OA_PROP_SAMPLE_OA is provided */
	bool sample;

	/** @exec_q: Exec queue corresponding to DRM_XE_OA_PROPERTY_EXEC_QUEUE_ID */
	struct xe_exec_queue *exec_q;

	/** @k_exec_q: kernel exec_q used for OA programming batch submissions */
	struct xe_exec_queue *k_exec_q;

	/** @enabled: Whether the stream is currently enabled */
	bool enabled;

	/** @oa_config: OA configuration used by the stream */
	struct xe_oa_config *oa_config;

	/** @oa_config_bos: List of struct @xe_oa_config_bo's */
	struct llist_head oa_config_bos;

	/** @poll_check_timer: Timer to periodically check for data in the OA buffer */
	struct hrtimer poll_check_timer;

	/** @poll_wq: Wait queue for waiting for OA data to be available */
	wait_queue_head_t poll_wq;

	/** @pollin: Whether there is data available to read */
	bool pollin;

	/** @wait_num_reports: Number of reports to wait for before signalling pollin */
	int wait_num_reports;

	/** @periodic: Whether periodic sampling is currently enabled */
	bool periodic;

	/** @period_exponent: OA unit sampling frequency is derived from this */
	int period_exponent;

	/** @oa_buffer: OA buffer for the stream */
	struct xe_oa_buffer oa_buffer;

	/** @poll_period_ns: hrtimer period for checking OA buffer for available data */
	u64 poll_period_ns;

	/** @override_gucrc: GuC RC has been overridden for the OA stream */
	bool override_gucrc;

	/** @oa_status: temporary storage for oa_status register value */
	u32 oa_status;

	/** @no_preempt: Whether preemption and timeslicing is disabled for stream exec_q */
	u32 no_preempt;

	/** @xef: xe_file with which the stream was opened */
	struct xe_file *xef;

	/** @last_fence: fence to use in stream destroy when needed */
	struct dma_fence *last_fence;

	/** @num_syncs: size of @syncs array */
	u32 num_syncs;

	/** @syncs: syncs to wait on and to signal */
	struct xe_sync_entry *syncs;
};
#endif
