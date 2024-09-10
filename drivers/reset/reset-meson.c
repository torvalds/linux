// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Amlogic Meson Reset Controller driver
 *
 * Copyright (c) 2016 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

struct meson_reset_param {
	unsigned int reg_count;
	unsigned int level_offset;
};

struct meson_reset {
	const struct meson_reset_param *param;
	struct reset_controller_dev rcdev;
	struct regmap *map;
};

static void meson_reset_offset_and_bit(struct meson_reset *data,
				       unsigned long id,
				       unsigned int *offset,
				       unsigned int *bit)
{
	unsigned int stride = regmap_get_reg_stride(data->map);

	*offset = (id / (stride * BITS_PER_BYTE)) * stride;
	*bit = id % (stride * BITS_PER_BYTE);
}

static int meson_reset_reset(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int offset, bit;

	meson_reset_offset_and_bit(data, id, &offset, &bit);

	return regmap_write(data->map, offset, BIT(bit));
}

static int meson_reset_level(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int offset, bit;

	meson_reset_offset_and_bit(data, id, &offset, &bit);
	offset += data->param->level_offset;

	return regmap_update_bits(data->map, offset,
				  BIT(bit), assert ? 0 : BIT(bit));
}

static int meson_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return meson_reset_level(rcdev, id, true);
}

static int meson_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return meson_reset_level(rcdev, id, false);
}

static const struct reset_control_ops meson_reset_ops = {
	.reset		= meson_reset_reset,
	.assert		= meson_reset_assert,
	.deassert	= meson_reset_deassert,
};

static const struct meson_reset_param meson8b_param = {
	.reg_count	= 8,
	.level_offset	= 0x7c,
};

static const struct meson_reset_param meson_a1_param = {
	.reg_count	= 3,
	.level_offset	= 0x40,
};

static const struct meson_reset_param meson_s4_param = {
	.reg_count	= 6,
	.level_offset	= 0x40,
};

static const struct meson_reset_param t7_param = {
	.reg_count      = 7,
	.level_offset   = 0x40,
};

static const struct of_device_id meson_reset_dt_ids[] = {
	 { .compatible = "amlogic,meson8b-reset",    .data = &meson8b_param},
	 { .compatible = "amlogic,meson-gxbb-reset", .data = &meson8b_param},
	 { .compatible = "amlogic,meson-axg-reset",  .data = &meson8b_param},
	 { .compatible = "amlogic,meson-a1-reset",   .data = &meson_a1_param},
	 { .compatible = "amlogic,meson-s4-reset",   .data = &meson_s4_param},
	 { .compatible = "amlogic,c3-reset",   .data = &meson_s4_param},
	 { .compatible = "amlogic,t7-reset",   .data = &t7_param},
	 { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, meson_reset_dt_ids);

static const struct regmap_config regmap_config = {
	.reg_bits   = 32,
	.val_bits   = 32,
	.reg_stride = 4,
};

static int meson_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_reset *data;
	void __iomem *base;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data->param = device_get_match_data(dev);
	if (!data->param)
		return -ENODEV;

	data->map = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(data->map))
		return dev_err_probe(dev, PTR_ERR(data->map),
				     "can't init regmap mmio region\n");

	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = data->param->reg_count * BITS_PER_BYTE
		* regmap_config.reg_stride;
	data->rcdev.ops = &meson_reset_ops;
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static struct platform_driver meson_reset_driver = {
	.probe	= meson_reset_probe,
	.driver = {
		.name		= "meson_reset",
		.of_match_table	= meson_reset_dt_ids,
	},
};
module_platform_driver(meson_reset_driver);

MODULE_DESCRIPTION("Amlogic Meson Reset Controller driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
