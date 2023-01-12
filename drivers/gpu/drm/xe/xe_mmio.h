/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _XE_MMIO_H_
#define _XE_MMIO_H_

#include <linux/delay.h>

#include "xe_gt_types.h"

struct drm_device;
struct drm_file;
struct xe_device;

int xe_mmio_init(struct xe_device *xe);

static inline u8 xe_mmio_read8(struct xe_gt *gt, u32 reg)
{
	if (reg < gt->mmio.adj_limit)
		reg += gt->mmio.adj_offset;

	return readb(gt->mmio.regs + reg);
}

static inline void xe_mmio_write32(struct xe_gt *gt,
				   u32 reg, u32 val)
{
	if (reg < gt->mmio.adj_limit)
		reg += gt->mmio.adj_offset;

	writel(val, gt->mmio.regs + reg);
}

static inline u32 xe_mmio_read32(struct xe_gt *gt, u32 reg)
{
	if (reg < gt->mmio.adj_limit)
		reg += gt->mmio.adj_offset;

	return readl(gt->mmio.regs + reg);
}

static inline u32 xe_mmio_rmw32(struct xe_gt *gt, u32 reg, u32 mask,
				 u32 val)
{
	u32 old, reg_val;

	old = xe_mmio_read32(gt, reg);
	reg_val = (old & mask) | val;
	xe_mmio_write32(gt, reg, reg_val);

	return old;
}

static inline void xe_mmio_write64(struct xe_gt *gt,
				   u32 reg, u64 val)
{
	if (reg < gt->mmio.adj_limit)
		reg += gt->mmio.adj_offset;

	writeq(val, gt->mmio.regs + reg);
}

static inline u64 xe_mmio_read64(struct xe_gt *gt, u32 reg)
{
	if (reg < gt->mmio.adj_limit)
		reg += gt->mmio.adj_offset;

	return readq(gt->mmio.regs + reg);
}

static inline int xe_mmio_write32_and_verify(struct xe_gt *gt,
					     u32 reg, u32 val,
					     u32 mask, u32 eval)
{
	u32 reg_val;

	xe_mmio_write32(gt, reg, val);
	reg_val = xe_mmio_read32(gt, reg);

	return (reg_val & mask) != eval ? -EINVAL : 0;
}

static inline int xe_mmio_wait32(struct xe_gt *gt, u32 reg, u32 val,
				 u32 mask, u32 timeout_us, u32 *out_val)
{
	ktime_t cur = ktime_get_raw();
	const ktime_t end = ktime_add_us(cur, timeout_us);
	int ret = -ETIMEDOUT;
	s64 wait = 10;
	u32 read;

	for (;;) {
		if ((xe_mmio_read32(gt, reg) & mask) == val)
			return 0;

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

		usleep_range(wait, wait << 1);
		wait <<= 1;
	}

	if (out_val)
		*out_val = read;

	return ret;
}

int xe_mmio_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file);

static inline bool xe_mmio_in_range(const struct xe_mmio_range *range, u32 reg)
{
	return range && reg >= range->start && reg <= range->end;
}

int xe_mmio_probe_vram(struct xe_device *xe);

#endif
