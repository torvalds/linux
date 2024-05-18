/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2023 Intel Corporation
 */

#ifndef _XE_MMIO_H_
#define _XE_MMIO_H_

#include "xe_gt_types.h"

struct xe_device;
struct xe_reg;

#define LMEM_BAR		2

int xe_mmio_init(struct xe_device *xe);
int xe_mmio_probe_tiles(struct xe_device *xe);

u8 xe_mmio_read8(struct xe_gt *gt, struct xe_reg reg);
u16 xe_mmio_read16(struct xe_gt *gt, struct xe_reg reg);
void xe_mmio_write32(struct xe_gt *gt, struct xe_reg reg, u32 val);
u32 xe_mmio_read32(struct xe_gt *gt, struct xe_reg reg);
u32 xe_mmio_rmw32(struct xe_gt *gt, struct xe_reg reg, u32 clr, u32 set);
int xe_mmio_write32_and_verify(struct xe_gt *gt, struct xe_reg reg, u32 val, u32 mask, u32 eval);
bool xe_mmio_in_range(const struct xe_gt *gt, const struct xe_mmio_range *range, struct xe_reg reg);

int xe_mmio_probe_vram(struct xe_device *xe);
u64 xe_mmio_read64_2x32(struct xe_gt *gt, struct xe_reg reg);
int xe_mmio_wait32(struct xe_gt *gt, struct xe_reg reg, u32 mask, u32 val, u32 timeout_us,
		   u32 *out_val, bool atomic);
int xe_mmio_wait32_not(struct xe_gt *gt, struct xe_reg reg, u32 mask, u32 val, u32 timeout_us,
		       u32 *out_val, bool atomic);

static inline u32 xe_mmio_adjusted_addr(const struct xe_gt *gt, u32 addr)
{
	if (addr < gt->mmio.adj_limit)
		addr += gt->mmio.adj_offset;
	return addr;
}

#endif
