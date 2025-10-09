/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef __I915_MMIO_RANGE_H__
#define __I915_MMIO_RANGE_H__

#include <linux/types.h>

/* Other register ranges (e.g., shadow tables, MCR tables, etc.) */
struct i915_mmio_range {
	u32 start;
	u32 end;
};

bool i915_mmio_range_table_contains(u32 addr, const struct i915_mmio_range *table);

#endif /* __I915_MMIO_RANGE_H__ */
