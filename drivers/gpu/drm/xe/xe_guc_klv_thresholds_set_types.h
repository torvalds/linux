/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GUC_KLV_THRESHOLDS_SET_TYPES_H_
#define _XE_GUC_KLV_THRESHOLDS_SET_TYPES_H_

#include "xe_args.h"

/**
 * MAKE_XE_GUC_KLV_THRESHOLDS_SET - Generate various GuC thresholds definitions.
 * @define: name of the inner macro to expand.
 *
 * The GuC firmware is able to monitor VF's adverse activity and will notify the
 * PF driver once any threshold is exceeded.
 *
 * This super macro allows various conversions between the GuC adverse event
 * threshold KLV definitions and the driver code without repeating similar code
 * or risking missing some cases.
 *
 * For each GuC threshold definition, the inner macro &define will be provided
 * with the &TAG, that corresponds to the GuC threshold KLV key name defined by
 * ABI and the associated &NAME, that may be used in code or debugfs/sysfs::
 *
 *	define(TAG, NAME)
 */
#define MAKE_XE_GUC_KLV_THRESHOLDS_SET(define)		\
	define(CAT_ERR, cat_error_count)		\
	define(ENGINE_RESET, engine_reset_count)	\
	define(PAGE_FAULT, page_fault_count)		\
	define(H2G_STORM, guc_time_us)			\
	define(IRQ_STORM, irq_time_us)			\
	define(DOORBELL_STORM, doorbell_time_us)	\
	/* end */

/**
 * XE_GUC_KLV_NUM_THRESHOLDS - Number of GuC thresholds KLVs.
 *
 * Calculated automatically using &MAKE_XE_GUC_KLV_THRESHOLDS_SET.
 */
#define XE_GUC_KLV_NUM_THRESHOLDS \
	(CALL_ARGS(COUNT_ARGS, MAKE_XE_GUC_KLV_THRESHOLDS_SET(ARGS_SEP_COMMA)) - 1)

/**
 * MAKE_XE_GUC_KLV_THRESHOLD_INDEX - Create enumerator name.
 * @TAG: unique TAG of the enum xe_guc_klv_threshold_index.
 */
#define MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG) \
	CONCATENATE(XE_GUC_KLV_THRESHOLD_INDEX_, TAG)

/**
 * enum xe_guc_klv_threshold_index - Index of the tracked GuC threshold.
 *
 * This enum is automatically generated using &MAKE_XE_GUC_KLV_THRESHOLDS_SET.
 * All these generated enumerators will only be used by the also generated code.
 */
enum xe_guc_klv_threshold_index {
#define define_xe_guc_klv_threshold_index_enum(TAG, ...)	\
								\
	MAKE_XE_GUC_KLV_THRESHOLD_INDEX(TAG),

	/* private: auto-generated enum definitions */
	MAKE_XE_GUC_KLV_THRESHOLDS_SET(define_xe_guc_klv_threshold_index_enum)
#undef define_xe_guc_klv_threshold_index_enum
};

#endif
