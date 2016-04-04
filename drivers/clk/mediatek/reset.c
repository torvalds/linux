/*
 * Copyright (c) 2014 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#include "clk-mtk.h"

struct mtk_reset {
	struct regmap *regmap;
	int regofs;
	struct reset_controller_dev rcdev;
};

static int mtk_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);

	return regmap_update_bits(data->regmap, data->regofs + ((id / 32) << 2),
			BIT(id % 32), ~0);
}

static int mtk_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);

	return regmap_update_bits(data->regmap, data->regofs + ((id / 32) << 2),
			BIT(id % 32), 0);
}

static int mtk_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret;

	ret = mtk_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return mtk_reset_deassert(rcdev, id);
}

static const struct reset_control_ops mtk_reset_ops = {
	.assert = mtk_reset_assert,
	.deassert = mtk_reset_deassert,
	.reset = mtk_reset,
};

void mtk_register_reset_controller(struct device_node *np,
			unsigned int num_regs, int regofs)
{
	struct mtk_reset *data;
	int ret;
	struct regmap *regmap;

	regmap = syscon_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %s: %ld\n", np->full_name,
				PTR_ERR(regmap));
		return;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->regmap = regmap;
	data->regofs = regofs;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = num_regs * 32;
	data->rcdev.ops = &mtk_reset_ops;
	data->rcdev.of_node = np;

	ret = reset_controller_register(&data->rcdev);
	if (ret) {
		pr_err("could not register reset controller: %d\n", ret);
		kfree(data);
		return;
	}
}
