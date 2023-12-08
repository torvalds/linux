// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti ARM SoC reset controller
 *
 * Copyright (c) 2021 TOSHIBA CORPORATION
 * Copyright (c) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "reset.h"

static inline struct visconti_reset *to_visconti_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct visconti_reset, rcdev);
}

static int visconti_reset_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct visconti_reset *reset = to_visconti_reset(rcdev);
	const struct visconti_reset_data *data = &reset->resets[id];
	u32 rst = BIT(data->rs_idx);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(reset->lock, flags);
	ret = regmap_update_bits(reset->regmap, data->rson_offset, rst, rst);
	spin_unlock_irqrestore(reset->lock, flags);

	return ret;
}

static int visconti_reset_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct visconti_reset *reset = to_visconti_reset(rcdev);
	const struct visconti_reset_data *data = &reset->resets[id];
	u32 rst = BIT(data->rs_idx);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(reset->lock, flags);
	ret = regmap_update_bits(reset->regmap, data->rsoff_offset, rst, rst);
	spin_unlock_irqrestore(reset->lock, flags);

	return ret;
}

static int visconti_reset_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	visconti_reset_assert(rcdev, id);
	udelay(1);
	visconti_reset_deassert(rcdev, id);

	return 0;
}

static int visconti_reset_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct visconti_reset *reset = to_visconti_reset(rcdev);
	const struct visconti_reset_data *data = &reset->resets[id];
	unsigned long flags;
	u32 reg;
	int ret;

	spin_lock_irqsave(reset->lock, flags);
	ret = regmap_read(reset->regmap, data->rson_offset, &reg);
	spin_unlock_irqrestore(reset->lock, flags);
	if (ret)
		return ret;

	return !(reg & data->rs_idx);
}

const struct reset_control_ops visconti_reset_ops = {
	.assert		= visconti_reset_assert,
	.deassert	= visconti_reset_deassert,
	.reset		= visconti_reset_reset,
	.status		= visconti_reset_status,
};

int visconti_register_reset_controller(struct device *dev,
				       struct regmap *regmap,
				       const struct visconti_reset_data *resets,
				       unsigned int num_resets,
				       const struct reset_control_ops *reset_ops,
				       spinlock_t *lock)
{
	struct visconti_reset *reset;

	reset = devm_kzalloc(dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	reset->regmap = regmap;
	reset->resets = resets;
	reset->rcdev.ops = reset_ops;
	reset->rcdev.nr_resets = num_resets;
	reset->rcdev.of_node = dev->of_node;
	reset->lock = lock;

	return devm_reset_controller_register(dev, &reset->rcdev);
}
