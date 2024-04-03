/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021-2023 Intel Corporation
 */

#ifndef _XE_MMIO_H_
#define _XE_MMIO_H_

#include <linux/delay.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "regs/xe_reg_defs.h"
#include "xe_device_types.h"
#include "xe_gt_printk.h"
#include "xe_gt_types.h"

struct drm_device;
struct drm_file;
struct xe_device;

#define LMEM_BAR		2

int xe_mmio_init(struct xe_device *xe);
int xe_mmio_root_tile_init(struct xe_device *xe);
void xe_mmio_probe_tiles(struct xe_device *xe);

static inline u8 xe_mmio_read8(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readb((reg.ext ? tile->mmio_ext.regs : tile->mmio.regs) + reg.addr);
}

static inline u16 xe_mmio_read16(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readw((reg.ext ? tile->mmio_ext.regs : tile->mmio.regs) + reg.addr);
}

static inline void xe_mmio_write32(struct xe_gt *gt,
				   struct xe_reg reg, u32 val)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	writel(val, (reg.ext ? tile->mmio_ext.regs : tile->mmio.regs) + reg.addr);
}

static inline u32 xe_mmio_read32(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readl((reg.ext ? tile->mmio_ext.regs : tile->mmio.regs) + reg.addr);
}

static inline u32 xe_mmio_rmw32(struct xe_gt *gt, struct xe_reg reg, u32 clr,
				u32 set)
{
	u32 old, reg_val;

	old = xe_mmio_read32(gt, reg);
	reg_val = (old & ~clr) | set;
	xe_mmio_write32(gt, reg, reg_val);

	return old;
}

static inline int xe_mmio_write32_and_verify(struct xe_gt *gt,
					     struct xe_reg reg, u32 val,
					     u32 mask, u32 eval)
{
	u32 reg_val;

	xe_mmio_write32(gt, reg, val);
	reg_val = xe_mmio_read32(gt, reg);

	return (reg_val & mask) != eval ? -EINVAL : 0;
}

static inline bool xe_mmio_in_range(const struct xe_gt *gt,
				    const struct xe_mmio_range *range,
				    struct xe_reg reg)
{
	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return range && reg.addr >= range->start && reg.addr <= range->end;
}

int xe_mmio_probe_vram(struct xe_device *xe);
u64 xe_mmio_read64_2x32(struct xe_gt *gt, struct xe_reg reg);
int xe_mmio_wait32(struct xe_gt *gt, struct xe_reg reg, u32 mask, u32 val, u32 timeout_us,
		   u32 *out_val, bool atomic);

#endif
