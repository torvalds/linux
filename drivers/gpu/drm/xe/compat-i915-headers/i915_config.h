/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __I915_CONFIG_H__
#define __I915_CONFIG_H__

#include <linux/sched.h>

static inline unsigned long i915_fence_timeout(void)
{
	return MAX_SCHEDULE_TIMEOUT;
}

#endif /* __I915_CONFIG_H__ */
