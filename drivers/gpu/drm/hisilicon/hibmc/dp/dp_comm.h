/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef DP_COMM_H
#define DP_COMM_H

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/io.h>
#include <drm/display/drm_dp_helper.h>

struct hibmc_dp_dev {
	struct drm_dp_aux aux;
	struct drm_device *dev;
	void __iomem *base;
	struct mutex lock; /* protects concurrent RW in hibmc_dp_reg_write_field() */
};

#define dp_field_modify(reg_value, mask, val)				\
	do {								\
		(reg_value) &= ~(mask);					\
		(reg_value) |= FIELD_PREP(mask, val);			\
	} while (0)							\

#define hibmc_dp_reg_write_field(dp, offset, mask, val)			\
	do {								\
		typeof(dp) _dp = dp;					\
		typeof(_dp->base) addr = (_dp->base + (offset));	\
		mutex_lock(&_dp->lock);					\
		u32 reg_value = readl(addr);				\
		dp_field_modify(reg_value, mask, val);			\
		writel(reg_value, addr);				\
		mutex_unlock(&_dp->lock);				\
	} while (0)

void hibmc_dp_aux_init(struct hibmc_dp_dev *dp);

#endif
