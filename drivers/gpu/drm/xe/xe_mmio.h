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
#include "xe_gt_types.h"

struct drm_device;
struct drm_file;
struct xe_device;

#define GEN12_LMEM_BAR		2

int xe_mmio_init(struct xe_device *xe);

static inline u8 xe_mmio_read8(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readb(tile->mmio.regs + reg.addr);
}

static inline u16 xe_mmio_read16(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readw(tile->mmio.regs + reg.addr);
}

static inline void xe_mmio_write32(struct xe_gt *gt,
				   struct xe_reg reg, u32 val)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	writel(val, tile->mmio.regs + reg.addr);
}

static inline u32 xe_mmio_read32(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readl(tile->mmio.regs + reg.addr);
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

static inline void xe_mmio_write64(struct xe_gt *gt,
				   struct xe_reg reg, u64 val)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	writeq(val, tile->mmio.regs + reg.addr);
}

static inline u64 xe_mmio_read64(struct xe_gt *gt, struct xe_reg reg)
{
	struct xe_tile *tile = gt_to_tile(gt);

	if (reg.addr < gt->mmio.adj_limit)
		reg.addr += gt->mmio.adj_offset;

	return readq(tile->mmio.regs + reg.addr);
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

static inline int xe_mmio_wait32(struct xe_gt *gt, struct xe_reg reg, u32 val,
				 u32 mask, u32 timeout_us, u32 *out_val,
				 bool atomic)
{
	ktime_t cur = ktime_get_raw();
	const ktime_t end = ktime_add_us(cur, timeout_us);
	int ret = -ETIMEDOUT;
	s64 wait = 10;
	u32 read;

	for (;;) {
		read = xe_mmio_read32(gt, reg);
		if ((read & mask) == val) {
			ret = 0;
			break;
		}

		cur = ktime_get_raw();
		if (!ktime_before(cur, end))
			break;

		if (ktime_after(ktime_add_us(cur, wait), end))
			wait = ktime_us_delta(end, cur);

		if (atomic)
			udelay(wait);
		else
			usleep_range(wait, wait << 1);
		wait <<= 1;
	}

	if (out_val)
		*out_val = read;

	return ret;
}

int xe_mmio_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file);

static inline bool xe_mmio_in_range(const struct xe_mmio_range *range,
				    struct xe_reg reg)
{
	return range && reg.addr >= range->start && reg.addr <= range->end;
}

int xe_mmio_probe_vram(struct xe_device *xe);
int xe_mmio_tile_vram_size(struct xe_tile *tile, u64 *vram_size, u64 *tile_size, u64 *tile_base);

#endif
