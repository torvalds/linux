// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 HiSilicon Technologies Co., Ltd.
 *
 * Simple HiSilicon phase clock implementation.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk.h"

struct clk_hisi_phase {
	struct clk_hw	hw;
	void __iomem	*reg;
	u32		*phase_degrees;
	u32		*phase_regvals;
	u8		phase_num;
	u32		mask;
	u8		shift;
	u8		flags;
	spinlock_t	*lock;
};

#define to_clk_hisi_phase(_hw) container_of(_hw, struct clk_hisi_phase, hw)

static int hisi_phase_regval_to_degrees(struct clk_hisi_phase *phase,
					u32 regval)
{
	int i;

	for (i = 0; i < phase->phase_num; i++)
		if (phase->phase_regvals[i] == regval)
			return phase->phase_degrees[i];

	return -EINVAL;
}

static int hisi_clk_get_phase(struct clk_hw *hw)
{
	struct clk_hisi_phase *phase = to_clk_hisi_phase(hw);
	u32 regval;

	regval = readl(phase->reg);
	regval = (regval & phase->mask) >> phase->shift;

	return hisi_phase_regval_to_degrees(phase, regval);
}

static int hisi_phase_degrees_to_regval(struct clk_hisi_phase *phase,
					int degrees)
{
	int i;

	for (i = 0; i < phase->phase_num; i++)
		if (phase->phase_degrees[i] == degrees)
			return phase->phase_regvals[i];

	return -EINVAL;
}

static int hisi_clk_set_phase(struct clk_hw *hw, int degrees)
{
	struct clk_hisi_phase *phase = to_clk_hisi_phase(hw);
	unsigned long flags = 0;
	int regval;
	u32 val;

	regval = hisi_phase_degrees_to_regval(phase, degrees);
	if (regval < 0)
		return regval;

	spin_lock_irqsave(phase->lock, flags);

	val = readl(phase->reg);
	val &= ~phase->mask;
	val |= regval << phase->shift;
	writel(val, phase->reg);

	spin_unlock_irqrestore(phase->lock, flags);

	return 0;
}

static const struct clk_ops clk_phase_ops = {
	.get_phase = hisi_clk_get_phase,
	.set_phase = hisi_clk_set_phase,
};

struct clk *clk_register_hisi_phase(struct device *dev,
		const struct hisi_phase_clock *clks,
		void __iomem *base, spinlock_t *lock)
{
	struct clk_hisi_phase *phase;
	struct clk_init_data init;

	phase = devm_kzalloc(dev, sizeof(struct clk_hisi_phase), GFP_KERNEL);
	if (!phase)
		return ERR_PTR(-ENOMEM);

	init.name = clks->name;
	init.ops = &clk_phase_ops;
	init.flags = clks->flags;
	init.parent_names = clks->parent_names ? &clks->parent_names : NULL;
	init.num_parents = clks->parent_names ? 1 : 0;

	phase->reg = base + clks->offset;
	phase->shift = clks->shift;
	phase->mask = (BIT(clks->width) - 1) << clks->shift;
	phase->lock = lock;
	phase->phase_degrees = clks->phase_degrees;
	phase->phase_regvals = clks->phase_regvals;
	phase->phase_num = clks->phase_num;
	phase->hw.init = &init;

	return devm_clk_register(dev, &phase->hw);
}
EXPORT_SYMBOL_GPL(clk_register_hisi_phase);
