// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "i915_mmio_range.h"

bool i915_mmio_range_table_contains(u32 addr, const struct i915_mmio_range *table)
{
	while (table->start || table->end) {
		if (addr >= table->start && addr <= table->end)
			return true;

		table++;
	}

	return false;
}
