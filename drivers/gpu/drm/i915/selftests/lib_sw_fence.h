/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * lib_sw_fence.h - library routines for testing N:M synchronisation points
 *
 * Copyright (C) 2017 Intel Corporation
 */

#ifndef _LIB_SW_FENCE_H_
#define _LIB_SW_FENCE_H_

#include <linux/timer.h>

#include "../i915_sw_fence.h"

#ifdef CONFIG_LOCKDEP
#define onstack_fence_init(fence)				\
do {								\
	static struct lock_class_key __key;			\
								\
	__onstack_fence_init((fence), #fence, &__key);	\
} while (0)
#else
#define onstack_fence_init(fence)				\
	__onstack_fence_init((fence), NULL, NULL)
#endif

void __onstack_fence_init(struct i915_sw_fence *fence,
			  const char *name,
			  struct lock_class_key *key);
void onstack_fence_fini(struct i915_sw_fence *fence);

struct timed_fence {
	struct i915_sw_fence fence;
	struct timer_list timer;
};

void timed_fence_init(struct timed_fence *tf, unsigned long expires);
void timed_fence_fini(struct timed_fence *tf);

struct i915_sw_fence *heap_fence_create(gfp_t gfp);
void heap_fence_put(struct i915_sw_fence *fence);

#endif /* _LIB_SW_FENCE_H_ */
