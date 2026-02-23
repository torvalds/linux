/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I915_CONFIG_H__
#define __I915_CONFIG_H__

#include <linux/types.h>
#include <linux/limits.h>

unsigned long i915_fence_context_timeout(u64 context);

static inline unsigned long i915_fence_timeout(void)
{
	return i915_fence_context_timeout(U64_MAX);
}

#endif /* __I915_CONFIG_H__ */
