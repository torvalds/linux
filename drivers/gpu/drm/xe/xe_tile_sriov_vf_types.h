/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _XE_TILE_SRIOV_VF_TYPES_H_
#define _XE_TILE_SRIOV_VF_TYPES_H_

#include <linux/types.h>

/**
 * struct xe_tile_sriov_vf_selfconfig - VF configuration data.
 */
struct xe_tile_sriov_vf_selfconfig {
	/** @ggtt_base: assigned base offset of the GGTT region. */
	u64 ggtt_base;
	/** @ggtt_size: assigned size of the GGTT region. */
	u64 ggtt_size;
	/** @lmem_size: assigned size of the LMEM. */
	u64 lmem_size;
};

#endif
