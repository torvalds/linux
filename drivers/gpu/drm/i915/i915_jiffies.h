/* SPDX-License-Identifier: MIT */
/* Copyright © 2025 Intel Corporation */

#ifndef __I915_JIFFIES_H__
#define __I915_JIFFIES_H__

#include <linux/jiffies.h>

static inline unsigned long msecs_to_jiffies_timeout(const unsigned int m)
{
	unsigned long j = msecs_to_jiffies(m);

	return min_t(unsigned long, MAX_JIFFY_OFFSET, j + 1);
}

#endif /* __I915_JIFFIES_H__ */
