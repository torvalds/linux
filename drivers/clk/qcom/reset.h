/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_CLK_RESET_H__
#define __QCOM_CLK_RESET_H__

#include <linux/reset-controller.h>

struct qcom_reset_map {
	unsigned int reg;
	u8 bit;
	u16 udelay;
	u32 bitmask;
};

struct regmap;

struct qcom_reset_controller {
	const struct qcom_reset_map *reset_map;
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
};

#define to_qcom_reset_controller(r) \
	container_of(r, struct qcom_reset_controller, rcdev);

extern const struct reset_control_ops qcom_reset_ops;

#endif
