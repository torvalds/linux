/* SPDX-License-Identifier: MIT */
/* Copyright © 2026 Intel Corporation */

#ifndef _XE_DISPLAY_VMA_H_
#define _XE_DISPLAY_VMA_H_

#include <linux/refcount.h>

struct xe_bo;
struct xe_ggtt_node;

struct i915_vma {
	refcount_t ref;
	struct xe_bo *bo, *dpt;
	struct xe_ggtt_node *node;
};

#endif
