/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_SURVIVABILITY_MODE_TYPES_H_
#define _XE_SURVIVABILITY_MODE_TYPES_H_

#include <linux/limits.h>
#include <linux/types.h>

struct xe_survivability_info {
	char name[NAME_MAX];
	u32 reg;
	u32 value;
};

/**
 * struct xe_survivability: Contains survivability mode information
 */
struct xe_survivability {
	/** @info: struct that holds survivability info from scratch registers */
	struct xe_survivability_info *info;

	/** @size: number of scratch registers */
	u32 size;

	/** @boot_status: indicates critical/non critical boot failure */
	u8 boot_status;

	/** @mode: boolean to indicate survivability mode */
	bool mode;
};

#endif /* _XE_SURVIVABILITY_MODE_TYPES_H_ */
