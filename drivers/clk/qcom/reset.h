/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __QCOM_CLK_RESET_H__
#define __QCOM_CLK_RESET_H__

#include <linux/reset-controller.h>

struct qcom_reset_map {
	unsigned int reg;
	u8 bit;
};

struct regmap;

struct qcom_reset_controller {
	const struct qcom_reset_map *reset_map;
	struct regmap *regmap;
	struct reset_controller_dev rcdev;
};

#define to_qcom_reset_controller(r) \
	container_of(r, struct qcom_reset_controller, rcdev);

extern struct reset_control_ops qcom_reset_ops;

#endif
