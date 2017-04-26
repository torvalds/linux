/*
 * Copyright (C) 2013 STMicroelectronics Limited
 * Author: Stephen Gallimore <stephen.gallimore@st.com>
 *
 * Inspired by mach-imx/src.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "reset-syscfg.h"

/**
 * Reset channel regmap configuration
 *
 * @reset: regmap field for the channel's reset bit.
 * @ack: regmap field for the channel's ack bit (optional).
 */
struct syscfg_reset_channel {
	struct regmap_field *reset;
	struct regmap_field *ack;
};

/**
 * A reset controller which groups together a set of related reset bits, which
 * may be located in different system configuration registers.
 *
 * @rst: base reset controller structure.
 * @active_low: are the resets in this controller active low, i.e. clearing
 *              the reset bit puts the hardware into reset.
 * @channels: An array of reset channels for this controller.
 */
struct syscfg_reset_controller {
	struct reset_controller_dev rst;
	bool active_low;
	struct syscfg_reset_channel *channels;
};

#define to_syscfg_reset_controller(_rst) \
	container_of(_rst, struct syscfg_reset_controller, rst)

static int syscfg_reset_program_hw(struct reset_controller_dev *rcdev,
				   unsigned long idx, int assert)
{
	struct syscfg_reset_controller *rst = to_syscfg_reset_controller(rcdev);
	const struct syscfg_reset_channel *ch;
	u32 ctrl_val = rst->active_low ? !assert : !!assert;
	int err;

	if (idx >= rcdev->nr_resets)
		return -EINVAL;

	ch = &rst->channels[idx];

	err = regmap_field_write(ch->reset, ctrl_val);
	if (err)
		return err;

	if (ch->ack) {
		unsigned long timeout = jiffies + msecs_to_jiffies(1000);
		u32 ack_val;

		while (true) {
			err = regmap_field_read(ch->ack, &ack_val);
			if (err)
				return err;

			if (ack_val == ctrl_val)
				break;

			if (time_after(jiffies, timeout))
				return -ETIME;

			cpu_relax();
		}
	}

	return 0;
}

static int syscfg_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long idx)
{
	return syscfg_reset_program_hw(rcdev, idx, true);
}

static int syscfg_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long idx)
{
	return syscfg_reset_program_hw(rcdev, idx, false);
}

static int syscfg_reset_dev(struct reset_controller_dev *rcdev,
			    unsigned long idx)
{
	int err;

	err = syscfg_reset_assert(rcdev, idx);
	if (err)
		return err;

	return syscfg_reset_deassert(rcdev, idx);
}

static int syscfg_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long idx)
{
	struct syscfg_reset_controller *rst = to_syscfg_reset_controller(rcdev);
	const struct syscfg_reset_channel *ch;
	u32 ret_val = 0;
	int err;

	if (idx >= rcdev->nr_resets)
		return -EINVAL;

	ch = &rst->channels[idx];
	if (ch->ack)
		err = regmap_field_read(ch->ack, &ret_val);
	else
		err = regmap_field_read(ch->reset, &ret_val);
	if (err)
		return err;

	return rst->active_low ? !ret_val : !!ret_val;
}

static const struct reset_control_ops syscfg_reset_ops = {
	.reset    = syscfg_reset_dev,
	.assert   = syscfg_reset_assert,
	.deassert = syscfg_reset_deassert,
	.status   = syscfg_reset_status,
};

static int syscfg_reset_controller_register(struct device *dev,
				const struct syscfg_reset_controller_data *data)
{
	struct syscfg_reset_controller *rc;
	int i, err;

	rc = devm_kzalloc(dev, sizeof(*rc), GFP_KERNEL);
	if (!rc)
		return -ENOMEM;

	rc->channels = devm_kcalloc(dev, data->nr_channels,
				    sizeof(*rc->channels), GFP_KERNEL);
	if (!rc->channels)
		return -ENOMEM;

	rc->rst.ops = &syscfg_reset_ops,
	rc->rst.of_node = dev->of_node;
	rc->rst.nr_resets = data->nr_channels;
	rc->active_low = data->active_low;

	for (i = 0; i < data->nr_channels; i++) {
		struct regmap *map;
		struct regmap_field *f;
		const char *compatible = data->channels[i].compatible;

		map = syscon_regmap_lookup_by_compatible(compatible);
		if (IS_ERR(map))
			return PTR_ERR(map);

		f = devm_regmap_field_alloc(dev, map, data->channels[i].reset);
		if (IS_ERR(f))
			return PTR_ERR(f);

		rc->channels[i].reset = f;

		if (!data->wait_for_ack)
			continue;

		f = devm_regmap_field_alloc(dev, map, data->channels[i].ack);
		if (IS_ERR(f))
			return PTR_ERR(f);

		rc->channels[i].ack = f;
	}

	err = reset_controller_register(&rc->rst);
	if (!err)
		dev_info(dev, "registered\n");

	return err;
}

int syscfg_reset_probe(struct platform_device *pdev)
{
	struct device *dev = pdev ? &pdev->dev : NULL;
	const struct of_device_id *match;

	if (!dev || !dev->driver)
		return -ENODEV;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	return syscfg_reset_controller_register(dev, match->data);
}
