/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GT_IDLE_SYSFS_TYPES_H_
#define _XE_GT_IDLE_SYSFS_TYPES_H_

#include <linux/types.h>

struct xe_guc_pc;

/* States of GT Idle */
enum xe_gt_idle_state {
	GT_IDLE_C0,
	GT_IDLE_C6,
	GT_IDLE_UNKNOWN,
};

/**
 * struct xe_gt_idle - A struct that contains idle properties based of gt
 */
struct xe_gt_idle {
	/** @name: name */
	char name[16];
	/** @residency_multiplier: residency multiplier in ns */
	u32 residency_multiplier;
	/** @cur_residency: raw driver copy of idle residency */
	u64 cur_residency;
	/** @prev_residency: previous residency counter */
	u64 prev_residency;
	/** @idle_status: get the current idle state */
	enum xe_gt_idle_state (*idle_status)(struct xe_guc_pc *pc);
	/** @idle_residency: get idle residency counter */
	u64 (*idle_residency)(struct xe_guc_pc *pc);
};

#endif /* _XE_GT_IDLE_SYSFS_TYPES_H_ */
