// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset driver for the StarFive JH71X0 SoCs
 *
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

#include "reset-starfive-jh71x0.h"

struct jh71x0_reset {
	struct reset_controller_dev rcdev;
	/* protect registers against concurrent read-modify-write */
	spinlock_t lock;
	void __iomem *assert;
	void __iomem *status;
	const u32 *asserted;
};

static inline struct jh71x0_reset *
jh71x0_reset_from(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct jh71x0_reset, rcdev);
}

static int jh71x0_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct jh71x0_reset *data = jh71x0_reset_from(rcdev);
	unsigned long offset = id / 32;
	u32 mask = BIT(id % 32);
	void __iomem *reg_assert = data->assert + offset * sizeof(u32);
	void __iomem *reg_status = data->status + offset * sizeof(u32);
	u32 done = data->asserted ? data->asserted[offset] & mask : 0;
	u32 value;
	unsigned long flags;
	int ret;

	if (!assert)
		done ^= mask;

	spin_lock_irqsave(&data->lock, flags);

	value = readl(reg_assert);
	if (assert)
		value |= mask;
	else
		value &= ~mask;
	writel(value, reg_assert);

	/* if the associated clock is gated, deasserting might otherwise hang forever */
	ret = readl_poll_timeout_atomic(reg_status, value, (value & mask) == done, 0, 1000);

	spin_unlock_irqrestore(&data->lock, flags);
	return ret;
}

static int jh71x0_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return jh71x0_reset_update(rcdev, id, true);
}

static int jh71x0_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return jh71x0_reset_update(rcdev, id, false);
}

static int jh71x0_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret;

	ret = jh71x0_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return jh71x0_reset_deassert(rcdev, id);
}

static int jh71x0_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct jh71x0_reset *data = jh71x0_reset_from(rcdev);
	unsigned long offset = id / 32;
	u32 mask = BIT(id % 32);
	void __iomem *reg_status = data->status + offset * sizeof(u32);
	u32 value = readl(reg_status);

	if (!data->asserted)
		return !(value & mask);

	return !((value ^ data->asserted[offset]) & mask);
}

static const struct reset_control_ops jh71x0_reset_ops = {
	.assert		= jh71x0_reset_assert,
	.deassert	= jh71x0_reset_deassert,
	.reset		= jh71x0_reset_reset,
	.status		= jh71x0_reset_status,
};

int reset_starfive_jh71x0_register(struct device *dev, struct device_node *of_node,
				   void __iomem *assert, void __iomem *status,
				   const u32 *asserted, unsigned int nr_resets,
				   struct module *owner)
{
	struct jh71x0_reset *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rcdev.ops = &jh71x0_reset_ops;
	data->rcdev.owner = owner;
	data->rcdev.nr_resets = nr_resets;
	data->rcdev.dev = dev;
	data->rcdev.of_node = of_node;

	spin_lock_init(&data->lock);
	data->assert = assert;
	data->status = status;
	data->asserted = asserted;

	return devm_reset_controller_register(dev, &data->rcdev);
}
EXPORT_SYMBOL_GPL(reset_starfive_jh71x0_register);
