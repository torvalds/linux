// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset driver for the StarFive JH7100 SoC
 *
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 */

#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

#include "reset-starfive-jh71x0.h"

struct jh7100_reset {
	struct reset_controller_dev rcdev;
	/* protect registers against concurrent read-modify-write */
	spinlock_t lock;
	void __iomem *assert;
	void __iomem *status;
	const u64 *asserted;
};

static inline struct jh7100_reset *
jh7100_reset_from(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct jh7100_reset, rcdev);
}

static int jh7100_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct jh7100_reset *data = jh7100_reset_from(rcdev);
	unsigned long offset = BIT_ULL_WORD(id);
	u64 mask = BIT_ULL_MASK(id);
	void __iomem *reg_assert = data->assert + offset * sizeof(u64);
	void __iomem *reg_status = data->status + offset * sizeof(u64);
	u64 done = data->asserted ? data->asserted[offset] & mask : 0;
	u64 value;
	unsigned long flags;
	int ret;

	if (!assert)
		done ^= mask;

	spin_lock_irqsave(&data->lock, flags);

	value = readq(reg_assert);
	if (assert)
		value |= mask;
	else
		value &= ~mask;
	writeq(value, reg_assert);

	/* if the associated clock is gated, deasserting might otherwise hang forever */
	ret = readq_poll_timeout_atomic(reg_status, value, (value & mask) == done, 0, 1000);

	spin_unlock_irqrestore(&data->lock, flags);
	return ret;
}

static int jh7100_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return jh7100_reset_update(rcdev, id, true);
}

static int jh7100_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return jh7100_reset_update(rcdev, id, false);
}

static int jh7100_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret;

	ret = jh7100_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return jh7100_reset_deassert(rcdev, id);
}

static int jh7100_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct jh7100_reset *data = jh7100_reset_from(rcdev);
	unsigned long offset = BIT_ULL_WORD(id);
	u64 mask = BIT_ULL_MASK(id);
	void __iomem *reg_status = data->status + offset * sizeof(u64);
	u64 value = readq(reg_status);

	return !((value ^ data->asserted[offset]) & mask);
}

static const struct reset_control_ops jh7100_reset_ops = {
	.assert		= jh7100_reset_assert,
	.deassert	= jh7100_reset_deassert,
	.reset		= jh7100_reset_reset,
	.status		= jh7100_reset_status,
};

int reset_starfive_jh7100_register(struct device *dev, struct device_node *of_node,
				   void __iomem *assert, void __iomem *status,
				   const u64 *asserted, unsigned int nr_resets,
				   struct module *owner)
{
	struct jh7100_reset *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rcdev.ops = &jh7100_reset_ops;
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
EXPORT_SYMBOL_GPL(reset_starfive_jh7100_register);
