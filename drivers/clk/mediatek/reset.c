// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "reset.h"

static int mtk_reset_update(struct reset_controller_dev *rcdev,
			    unsigned long id, bool deassert)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);
	unsigned int val = deassert ? 0 : ~0;

	return regmap_update_bits(data->regmap,
				  data->regofs + ((id / 32) << 2),
				  BIT(id % 32), val);
}

static int mtk_reset_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	return mtk_reset_update(rcdev, id, false);
}

static int mtk_reset_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return mtk_reset_update(rcdev, id, true);
}

static int mtk_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	int ret;

	ret = mtk_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return mtk_reset_deassert(rcdev, id);
}

static int mtk_reset_update_set_clr(struct reset_controller_dev *rcdev,
				    unsigned long id, bool deassert)
{
	struct mtk_reset *data = container_of(rcdev, struct mtk_reset, rcdev);
	unsigned int deassert_ofs = deassert ? 0x4 : 0;

	return regmap_write(data->regmap,
			    data->regofs + ((id / 32) << 4) + deassert_ofs,
			    BIT(id % 32));
}

static int mtk_reset_assert_set_clr(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	return mtk_reset_update_set_clr(rcdev, id, false);
}

static int mtk_reset_deassert_set_clr(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	return mtk_reset_update_set_clr(rcdev, id, true);
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

void mtk_register_reset_controller(struct device_node *np,
				   u32 rst_bank_nr, u16 reg_ofs,
				   enum mtk_reset_version version)
{
	struct mtk_reset *data;
	int ret;
	struct regmap *regmap;
	const struct reset_control_ops *rcops = NULL;

	switch (version) {
	case MTK_RST_SIMPLE:
		rcops = &mtk_reset_ops;
		break;
	case MTK_RST_SET_CLR:
		rcops = &mtk_reset_ops_set_clr;
		break;
	default:
		pr_err("Unknown reset version %d\n", version);
		return;
	}

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", np, regmap);
		return;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return;

	data->regmap = regmap;
	data->regofs = reg_ofs;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = rst_bank_nr * 32;
	data->rcdev.ops = rcops;
	data->rcdev.of_node = np;

	ret = reset_controller_register(&data->rcdev);
	if (ret) {
		pr_err("could not register reset controller: %d\n", ret);
		kfree(data);
	}
}

MODULE_LICENSE("GPL");
