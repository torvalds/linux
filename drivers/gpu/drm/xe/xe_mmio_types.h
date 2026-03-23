/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2026 Intel Corporation
 */

#ifndef _XE_MMIO_TYPES_H_
#define _XE_MMIO_TYPES_H_

#include <linux/types.h>

struct xe_gt;
struct xe_tile;

/**
 * struct xe_mmio - register mmio structure
 *
 * Represents an MMIO region that the CPU may use to access registers.  A
 * region may share its IO map with other regions (e.g., all GTs within a
 * tile share the same map with their parent tile, but represent different
 * subregions of the overall IO space).
 */
struct xe_mmio {
	/** @tile: Backpointer to tile, used for tracing */
	struct xe_tile *tile;

	/** @regs: Map used to access registers. */
	void __iomem *regs;

	/**
	 * @sriov_vf_gt: Backpointer to GT.
	 *
	 * This pointer is only set for GT MMIO regions and only when running
	 * as an SRIOV VF structure
	 */
	struct xe_gt *sriov_vf_gt;

	/**
	 * @regs_size: Length of the register region within the map.
	 *
	 * The size of the iomap set in *regs is generally larger than the
	 * register mmio space since it includes unused regions and/or
	 * non-register regions such as the GGTT PTEs.
	 */
	size_t regs_size;

	/** @adj_limit: adjust MMIO address if address is below this value */
	u32 adj_limit;

	/** @adj_offset: offset to add to MMIO address when adjusting */
	u32 adj_offset;
};

/**
 * struct xe_mmio_range - register range structure
 *
 * @start: first register offset in the range.
 * @end: last register offset in the range.
 */
struct xe_mmio_range {
	u32 start;
	u32 end;
};

#endif
