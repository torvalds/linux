// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd..
 * All rights reserved.
 *
 * ESWIN EIC7700 HSP Reset Driver
 *
 * Authors: Xuyang Dong <dongxuyang@eswincomputing.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/reset/eswin,eic7700-hspcrg.h>

/**
 * struct eic7700_hsp_reset_data - reset controller information structure
 * @rcdev: reset controller entity
 * @regmap: regmap handle containing the memory-mapped reset registers
 */
struct eic7700_hsp_reset_data {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
};

struct eic7700_hsp_reg {
	u32 reg;
	u32 bit;
	bool active_low;
};

static inline struct eic7700_hsp_reset_data *
to_eic7700_hsp_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct eic7700_hsp_reset_data, rcdev);
}

static const struct eic7700_hsp_reg eic7700_hsp_reset[] = {
	[EIC7700_HSP_RST_SATA_P0]	= {0x340, BIT(0), false},
	[EIC7700_HSP_RST_SATA_PHY]	= {0x340, BIT(1), false},
	[EIC7700_HSP_RST_USB0]		= {0x800, BIT(24), true},
	[EIC7700_HSP_RST_USB1]		= {0x900, BIT(24), true},
	[EIC7700_HSP_RST_USB0_PHY]	= {0x800, BIT(25), false},
	[EIC7700_HSP_RST_USB1_PHY]	= {0x900, BIT(25), false},
};

static int eic7700_hsp_reset_assert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct eic7700_hsp_reset_data *data = to_eic7700_hsp_reset(rcdev);

	return regmap_assign_bits(data->regmap, eic7700_hsp_reset[id].reg,
				  eic7700_hsp_reset[id].bit,
				  !eic7700_hsp_reset[id].active_low);
}

static int eic7700_hsp_reset_deassert(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	struct eic7700_hsp_reset_data *data = to_eic7700_hsp_reset(rcdev);

	return regmap_assign_bits(data->regmap, eic7700_hsp_reset[id].reg,
				  eic7700_hsp_reset[id].bit,
				  eic7700_hsp_reset[id].active_low);
}

static const struct reset_control_ops eic7700_hsp_reset_ops = {
	.assert = eic7700_hsp_reset_assert,
	.deassert = eic7700_hsp_reset_deassert,
};

static int eic7700_hsp_reset_probe(struct auxiliary_device *adev,
				   const struct auxiliary_device_id *id)
{
	struct eic7700_hsp_reset_data *data;
	struct device *dev = &adev->dev;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = dev_get_regmap(dev->parent, NULL);
	if (!data->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap!\n");

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.ops = &eic7700_hsp_reset_ops;
	data->rcdev.of_node = dev->parent->of_node;
	data->rcdev.dev = dev;
	data->rcdev.nr_resets = ARRAY_SIZE(eic7700_hsp_reset);

	return devm_reset_controller_register(dev, &data->rcdev);
}

static const struct auxiliary_device_id eic7700_hsp_reset_ids[] = {
	{ .name = "clk_eic7700_hsp.hsp-reset", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(auxiliary, eic7700_hsp_reset_ids);

static struct auxiliary_driver eic7700_hsp_reset_driver = {
	.probe	= eic7700_hsp_reset_probe,
	.id_table = eic7700_hsp_reset_ids,
};

module_auxiliary_driver(eic7700_hsp_reset_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xuyang Dong <dongxuyang@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN EIC7700 HSP Reset Controller Driver");
