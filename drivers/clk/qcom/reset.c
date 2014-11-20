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

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/delay.h>

#include "reset.h"

static int qcom_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	rcdev->ops->assert(rcdev, id);
	udelay(1);
	rcdev->ops->deassert(rcdev, id);
	return 0;
}

static int
qcom_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct qcom_reset_controller *rst;
	const struct qcom_reset_map *map;
	u32 mask;

	rst = to_qcom_reset_controller(rcdev);
	map = &rst->reset_map[id];
	mask = BIT(map->bit);

	return regmap_update_bits(rst->regmap, map->reg, mask, mask);
}

static int
qcom_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct qcom_reset_controller *rst;
	const struct qcom_reset_map *map;
	u32 mask;

	rst = to_qcom_reset_controller(rcdev);
	map = &rst->reset_map[id];
	mask = BIT(map->bit);

	return regmap_update_bits(rst->regmap, map->reg, mask, 0);
}

struct reset_control_ops qcom_reset_ops = {
	.reset = qcom_reset,
	.assert = qcom_reset_assert,
	.deassert = qcom_reset_deassert,
};
EXPORT_SYMBOL_GPL(qcom_reset_ops);
