/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_PAT_H_
#define _XE_PAT_H_

#include <linux/types.h>

struct drm_printer;
struct xe_device;
struct xe_gt;

/**
 * struct xe_pat_table_entry - The pat_index encoding and other meta information.
 */
struct xe_pat_table_entry {
	/**
	 * @value: The platform specific value encoding the various memory
	 * attributes (this maps to some fixed pat_index). So things like
	 * caching, coherency, compression etc can be encoded here.
	 */
	u32 value;

	/**
	 * @coh_mode: The GPU coherency mode that @value maps to.
	 */
#define XE_COH_NONE          1
#define XE_COH_AT_LEAST_1WAY 2
	u16 coh_mode;
};

/**
 * xe_pat_init_early - SW initialization, setting up data based on device
 * @xe: xe device
 */
void xe_pat_init_early(struct xe_device *xe);

/**
 * xe_pat_init - Program HW PAT table
 * @gt: GT structure
 */
void xe_pat_init(struct xe_gt *gt);

/**
 * xe_pat_dump - Dump PAT table
 * @gt: GT structure
 * @p: Printer to dump info to
 */
void xe_pat_dump(struct xe_gt *gt, struct drm_printer *p);

/**
 * xe_pat_index_get_coh_mode - Extract the coherency mode for the given
 * pat_index.
 * @xe: xe device
 * @pat_index: The pat_index to query
 */
u16 xe_pat_index_get_coh_mode(struct xe_device *xe, u16 pat_index);

#endif
