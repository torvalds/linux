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

static inline struct mtk_clk_rst_data *to_mtk_clk_rst_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct mtk_clk_rst_data, rcdev);
}

static int mtk_reset_update(struct reset_controller_dev *rcdev,
			    unsigned long id, bool deassert)
{
	struct mtk_clk_rst_data *data = to_mtk_clk_rst_data(rcdev);
	unsigned int val = deassert ? 0 : ~0;

	return regmap_update_bits(data->regmap,
				  data->desc->rst_bank_ofs[id / RST_NR_PER_BANK],
				  BIT(id % RST_NR_PER_BANK), val);
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
	struct mtk_clk_rst_data *data = to_mtk_clk_rst_data(rcdev);
	unsigned int deassert_ofs = deassert ? 0x4 : 0;

	return regmap_write(data->regmap,
			    data->desc->rst_bank_ofs[id / RST_NR_PER_BANK] +
			    deassert_ofs,
			    BIT(id % RST_NR_PER_BANK));
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

static int reset_xlate(struct reset_controller_dev *rcdev,
		       const struct of_phandle_args *reset_spec)
{
	struct mtk_clk_rst_data *data = to_mtk_clk_rst_data(rcdev);

	if (reset_spec->args[0] >= rcdev->nr_resets ||
	    reset_spec->args[0] >= data->desc->rst_idx_map_nr)
		return -EINVAL;

	return data->desc->rst_idx_map[reset_spec->args[0]];
}

int mtk_register_reset_controller(struct device_node *np,
				  const struct mtk_clk_rst_desc *desc)
{
	struct regmap *regmap;
	const struct reset_control_ops *rcops = NULL;
	struct mtk_clk_rst_data *data;
	int ret;

	if (!desc) {
		pr_err("mtk clock reset desc is NULL\n");
		return -EINVAL;
	}

	switch (desc->version) {
	case MTK_RST_SIMPLE:
		rcops = &mtk_reset_ops;
		break;
	case MTK_RST_SET_CLR:
		rcops = &mtk_reset_ops_set_clr;
		break;
	default:
		pr_err("Unknown reset version %d\n", desc->version);
		return -EINVAL;
	}

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %pe\n", np, regmap);
		return -EINVAL;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->desc = desc;
	data->regmap = regmap;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = rcops;
	data->rcdev.of_node = np;

	if (data->desc->rst_idx_map_nr > 0) {
		data->rcdev.of_reset_n_cells = 1;
		data->rcdev.nr_resets = desc->rst_idx_map_nr;
		data->rcdev.of_xlate = reset_xlate;
	} else {
		data->rcdev.nr_resets = desc->rst_bank_nr * RST_NR_PER_BANK;
	}

	ret = reset_controller_register(&data->rcdev);
	if (ret) {
		pr_err("could not register reset controller: %d\n", ret);
		kfree(data);
		return ret;
	}

	return 0;
}

int mtk_register_reset_controller_with_dev(struct device *dev,
					   const struct mtk_clk_rst_desc *desc)
{
	struct device_node *np = dev->of_node;
	struct regmap *regmap;
	const struct reset_control_ops *rcops = NULL;
	struct mtk_clk_rst_data *data;
	int ret;

	if (!desc) {
		dev_err(dev, "mtk clock reset desc is NULL\n");
		return -EINVAL;
	}

	switch (desc->version) {
	case MTK_RST_SIMPLE:
		rcops = &mtk_reset_ops;
		break;
	case MTK_RST_SET_CLR:
		rcops = &mtk_reset_ops_set_clr;
		break;
	default:
		dev_err(dev, "Unknown reset version %d\n", desc->version);
		return -EINVAL;
	}

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Cannot find regmap %pe\n", regmap);
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->desc = desc;
	data->regmap = regmap;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = rcops;
	data->rcdev.of_node = np;
	data->rcdev.dev = dev;

	if (data->desc->rst_idx_map_nr > 0) {
		data->rcdev.of_reset_n_cells = 1;
		data->rcdev.nr_resets = desc->rst_idx_map_nr;
		data->rcdev.of_xlate = reset_xlate;
	} else {
		data->rcdev.nr_resets = desc->rst_bank_nr * RST_NR_PER_BANK;
	}

	ret = devm_reset_controller_register(dev, &data->rcdev);
	if (ret) {
		dev_err(dev, "could not register reset controller: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_register_reset_controller_with_dev);

MODULE_LICENSE("GPL");
