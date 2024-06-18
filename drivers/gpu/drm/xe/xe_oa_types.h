/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_OA_TYPES_H_
#define _XE_OA_TYPES_H_

#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <drm/xe_drm.h>
#include "regs/xe_reg_defs.h"

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

	/** @oa_formats: tracks all OA formats across platforms */
	const struct xe_oa_format *oa_formats;

	/** @format_mask: tracks valid OA formats for a platform */
	unsigned long format_mask[BITS_TO_LONGS(__XE_OA_FORMAT_MAX)];

	/** @oa_unit_ids: tracks oa unit ids assigned across gt's */
	u16 oa_unit_ids;
};
#endif
