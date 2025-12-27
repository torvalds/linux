/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SURVIVABILITY_MODE_TYPES_H_
#define _XE_SURVIVABILITY_MODE_TYPES_H_

#include <linux/limits.h>
#include <linux/types.h>

enum scratch_reg {
	CAPABILITY_INFO,
	POSTCODE_TRACE,
	POSTCODE_TRACE_OVERFLOW,
	AUX_INFO0,
	AUX_INFO1,
	AUX_INFO2,
	AUX_INFO3,
	AUX_INFO4,
	MAX_SCRATCH_REG,
};

enum xe_survivability_type {
	XE_SURVIVABILITY_TYPE_BOOT,
	XE_SURVIVABILITY_TYPE_RUNTIME,
};

/**
 * struct xe_survivability: Contains survivability mode information
 */
struct xe_survivability {
	/** @info: survivability debug info */
	u32 info[MAX_SCRATCH_REG];

	/** @size: number of scratch registers */
	u32 size;

	/** @boot_status: indicates critical/non critical boot failure */
	u8 boot_status;

	/** @mode: boolean to indicate survivability mode */
	bool mode;

	/** @type: survivability type */
	enum xe_survivability_type type;

	/** @fdo_mode: indicates if FDO mode is enabled */
	bool fdo_mode;

	/** @version: breadcrumb version of survivability mode  */
	u8 version;
};

#endif /* _XE_SURVIVABILITY_MODE_TYPES_H_ */
