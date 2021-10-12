// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
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

static int mtk_reset_assert_set_clr(struct reset_controller_dev *rcdev,
	unsigned long id)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);
	unsigned int reg = data->regofs + ((id / 32) << 4);

	return regmap_write(data->regmap, reg, 1);
}

static int mtk_reset_deassert_set_clr(struct reset_controller_dev *rcdev,
	unsigned long id)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);
	unsigned int reg = data->regofs + ((id / 32) << 4) + 0x4;

	return regmap_write(data->regmap, reg, 1);
}

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

static int mtk_reset_set_clr(struct reset_controller_dev *rcdev,
	unsigned long id)
{
	int ret;

	ret = mtk_reset_assert_set_clr(rcdev, id);
	if (ret)
		return ret;
	return mtk_reset_deassert_set_clr(rcdev, id);
}

static const struct reset_control_ops mtk_reset_ops = {
	.assert = mtk_reset_assert,
	.deassert = mtk_reset_deassert,
	.reset = mtk_reset,
};

static const struct reset_control_ops mtk_reset_ops_set_clr = {
	.assert = mtk_reset_assert_set_clr,
	.deassert = mtk_reset_deassert_set_clr,
	.reset = mtk_reset_set_clr,
};

static void mtk_register_reset_controller_common(struct device_node *np,
			unsigned int num_regs, int regofs,
			const struct reset_control_ops *reset_ops)
{
	struct mtk_reset *data;
	int ret;
	struct regmap *regmap;

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %ld\n", np,
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
	data->rcdev.ops = reset_ops;
	data->rcdev.of_node = np;

	ret = reset_controller_register(&data->rcdev);
	if (ret) {
		pr_err("could not register reset controller: %d\n", ret);
		kfree(data);
		return;
	}
}

void mtk_register_reset_controller(struct device_node *np,
	unsigned int num_regs, int regofs)
{
	mtk_register_reset_controller_common(np, num_regs, regofs,
		&mtk_reset_ops);
}

void mtk_register_reset_controller_set_clr(struct device_node *np,
	unsigned int num_regs, int regofs)
{
	mtk_register_reset_controller_common(np, num_regs, regofs,
		&mtk_reset_ops_set_clr);
}
