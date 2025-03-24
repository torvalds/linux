// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Amlogic Meson Reset core functions
 *
 * Copyright (c) 2016-2024 BayLibre, SAS.
 * Authors: Neil Armstrong <narmstrong@baylibre.com>
 *          Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include "reset-meson.h"

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
	offset += data->param->reset_offset;

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
	assert ^= data->param->level_low_reset;

	return regmap_update_bits(data->map, offset,
				  BIT(bit), assert ? BIT(bit) : 0);
}

static int meson_reset_status(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct meson_reset *data =
		container_of(rcdev, struct meson_reset, rcdev);
	unsigned int val, offset, bit;

	meson_reset_offset_and_bit(data, id, &offset, &bit);
	offset += data->param->level_offset;

	regmap_read(data->map, offset, &val);
	val = !!(BIT(bit) & val);

	return val ^ data->param->level_low_reset;
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

static int meson_reset_level_toggle(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	int ret;

	ret = meson_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return meson_reset_deassert(rcdev, id);
}

const struct reset_control_ops meson_reset_ops = {
	.reset		= meson_reset_reset,
	.assert		= meson_reset_assert,
	.deassert	= meson_reset_deassert,
	.status		= meson_reset_status,
};
EXPORT_SYMBOL_NS_GPL(meson_reset_ops, "MESON_RESET");

const struct reset_control_ops meson_reset_toggle_ops = {
	.reset		= meson_reset_level_toggle,
	.assert		= meson_reset_assert,
	.deassert	= meson_reset_deassert,
	.status		= meson_reset_status,
};
EXPORT_SYMBOL_NS_GPL(meson_reset_toggle_ops, "MESON_RESET");

int meson_reset_controller_register(struct device *dev, struct regmap *map,
				    const struct meson_reset_param *param)
{
	struct meson_reset *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->param = param;
	data->map = map;
	data->rcdev.owner = dev->driver->owner;
	data->rcdev.nr_resets = param->reset_num;
	data->rcdev.ops = data->param->reset_ops;
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}
EXPORT_SYMBOL_NS_GPL(meson_reset_controller_register, "MESON_RESET");

MODULE_DESCRIPTION("Amlogic Meson Reset Core function");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS("MESON_RESET");
