// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2022 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "reset-stm32.h"

struct stm32_reset_data {
	/* reset lock */
	spinlock_t			lock;
	struct reset_controller_dev	rcdev;
	void __iomem			*membase;
	u32				clear_offset;
};

static inline struct stm32_reset_data *
to_stm32_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct stm32_reset_data, rcdev);
}

static int stm32_reset_update(struct reset_controller_dev *rcdev,
			      unsigned long id, bool assert)
{
	struct stm32_reset_data *data = to_stm32_reset_data(rcdev);
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);

	if (data->clear_offset) {
		void __iomem *addr;

		addr = data->membase + (bank * reg_width);
		if (!assert)
			addr += data->clear_offset;

		writel(BIT(offset), addr);

	} else {
		unsigned long flags;
		u32 reg;

		spin_lock_irqsave(&data->lock, flags);

		reg = readl(data->membase + (bank * reg_width));

		if (assert)
			reg |= BIT(offset);
		else
			reg &= ~BIT(offset);

		writel(reg, data->membase + (bank * reg_width));

		spin_unlock_irqrestore(&data->lock, flags);
	}

	return 0;
}

static int stm32_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return stm32_reset_update(rcdev, id, true);
}

static int stm32_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return stm32_reset_update(rcdev, id, false);
}

static int stm32_reset_status(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct stm32_reset_data *data = to_stm32_reset_data(rcdev);
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	u32 reg;

	reg = readl(data->membase + (bank * reg_width));

	return !!(reg & BIT(offset));
}

static const struct reset_control_ops stm32_reset_ops = {
	.assert		= stm32_reset_assert,
	.deassert	= stm32_reset_deassert,
	.status		= stm32_reset_status,
};

int stm32_rcc_reset_init(struct device *dev, struct clk_stm32_reset_data *data,
			 void __iomem *base)
{
	struct stm32_reset_data *reset_data;

	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		return -ENOMEM;

	spin_lock_init(&reset_data->lock);

	reset_data->membase = base;
	reset_data->rcdev.owner = THIS_MODULE;
	reset_data->rcdev.ops = &stm32_reset_ops;
	reset_data->rcdev.of_node = dev_of_node(dev);
	reset_data->rcdev.nr_resets = data->nr_lines;
	reset_data->clear_offset = data->clear_offset;

	return reset_controller_register(&reset_data->rcdev);
}
