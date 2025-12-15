/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_GT_SRIOV_PF_MIGRATION_TYPES_H_
#define _XE_GT_SRIOV_PF_MIGRATION_TYPES_H_

#include <linux/ptr_ring.h>

/**
 * struct xe_gt_sriov_migration_data - GT-level per-VF migration data.
 *
 * Used by the PF driver to maintain per-VF migration data.
 */
struct xe_gt_sriov_migration_data {
	/** @ring: queue containing VF save / restore migration data */
	struct ptr_ring ring;
	/** @save: structure for currently processed save migration data */
	struct {
		/** @save.data_remaining: bitmap of migration types that need to be saved */
		unsigned long data_remaining;
		/** @save.vram_offset: last saved offset within VRAM, used for chunked VRAM save */
		loff_t vram_offset;
	} save;
};

#endif
