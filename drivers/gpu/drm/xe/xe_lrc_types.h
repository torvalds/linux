/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_LRC_TYPES_H_
#define _XE_LRC_TYPES_H_

#include "xe_hw_fence_types.h"

struct xe_bo;

/**
 * struct xe_lrc - Logical ring context (LRC) and submission ring object
 */
struct xe_lrc {
	/**
	 * @bo: buffer object (memory) for logical ring context, per process HW
	 * status page, and submission ring.
	 */
	struct xe_bo *bo;

	/** @tile: tile which this LRC belongs to */
	struct xe_tile *tile;

	/** @flags: LRC flags */
	u32 flags;

	/** @ring: submission ring state */
	struct {
		/** @ring.size: size of submission ring */
		u32 size;
		/** @ring.tail: tail of submission ring */
		u32 tail;
		/** @ring.old_tail: shadow of tail */
		u32 old_tail;
	} ring;

	/** @desc: LRC descriptor */
	u64 desc;

	/** @fence_ctx: context for hw fence */
	struct xe_hw_fence_ctx fence_ctx;
};

#endif
