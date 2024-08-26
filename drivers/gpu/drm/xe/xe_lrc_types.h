/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

#ifndef _XE_LRC_TYPES_H_
#define _XE_LRC_TYPES_H_

#include <linux/kref.h>

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

	/** @size: size of lrc including any indirect ring state page */
	u32 size;

	/** @tile: tile which this LRC belongs to */
	struct xe_tile *tile;

	/** @flags: LRC flags */
#define XE_LRC_FLAG_INDIRECT_RING_STATE		0x1
	u32 flags;

	/** @refcount: ref count of this lrc */
	struct kref refcount;

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

	/** @ctx_timestamp: readout value of CTX_TIMESTAMP on last update */
	u32 ctx_timestamp;
};

struct xe_lrc_snapshot;

#endif
