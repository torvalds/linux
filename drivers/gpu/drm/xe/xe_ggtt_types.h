/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_GGTT_TYPES_H_
#define _XE_GGTT_TYPES_H_

#include <drm/drm_mm.h>

#include "xe_pt_types.h"

struct xe_bo;
struct xe_gt;

struct xe_ggtt {
	struct xe_tile *tile;

	u64 size;

#define XE_GGTT_FLAGS_64K BIT(0)
	unsigned int flags;

	struct xe_bo *scratch;

	struct mutex lock;

	u64 __iomem *gsm;

	const struct xe_ggtt_pt_ops *pt_ops;

	struct drm_mm mm;

	/** @access_count: counts GGTT writes */
	unsigned int access_count;
};

struct xe_ggtt_pt_ops {
	u64 (*pte_encode_bo)(struct xe_bo *bo, u64 bo_offset, u16 pat_index);
	void (*ggtt_set_pte)(struct xe_ggtt *ggtt, u64 addr, u64 pte);
};

#endif
