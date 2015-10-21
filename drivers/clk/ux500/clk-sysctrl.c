/*
 * Sysctrl clock implementation for ux500 platform.
 *
 * Copyright (C) 2013 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk-provider.h>
#include <linux/mfd/abx500/ab8500-sysctrl.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/err.h>
#include "clk.h"

#define SYSCTRL_MAX_NUM_PARENTS 4

#define to_clk_sysctrl(_hw) container_of(_hw, struct clk_sysctrl, hw)

struct clk_sysctrl {
	struct clk_hw hw;
	struct device *dev;
	u8 parent_index;
	u16 reg_sel[SYSCTRL_MAX_NUM_PARENTS];
	u8 reg_mask[SYSCTRL_MAX_NUM_PARENTS];
	u8 reg_bits[SYSCTRL_MAX_NUM_PARENTS];
	unsigned long rate;
	unsigned long enable_delay_us;
};

/* Sysctrl clock operations. */

static int clk_sysctrl_prepare(struct clk_hw *hw)
{
	int ret;
	struct clk_sysctrl *clk = to_clk_sysctrl(hw);

	ret = ab8500_sysctrl_write(clk->reg_sel[0], clk->reg_mask[0],
				clk->reg_bits[0]);

	if (!ret && clk->enable_delay_us)
		usleep_range(clk->enable_delay_us, clk->enable_delay_us);

	return ret;
}

static void clk_sysctrl_unprepare(struct clk_hw *hw)
{
	struct clk_sysctrl *clk = to_clk_sysctrl(hw);
	if (ab8500_sysctrl_clear(clk->reg_sel[0], clk->reg_mask[0]))
		dev_err(clk->dev, "clk_sysctrl: %s fail to clear %s.\n",
			__func__, clk_hw_get_name(hw));
}

static unsigned long clk_sysctrl_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_sysctrl *clk = to_clk_sysctrl(hw);
	return clk->rate;
}

static int clk_sysctrl_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sysctrl *clk = to_clk_sysctrl(hw);
	u8 old_index = clk->parent_index;
	int ret = 0;

	if (clk->reg_sel[old_index]) {
		ret = ab8500_sysctrl_clear(clk->reg_sel[old_index],
					clk->reg_mask[old_index]);
		if (ret)
			return ret;
	}

	if (clk->reg_sel[index]) {
		ret = ab8500_sysctrl_write(clk->reg_sel[index],
					clk->reg_mask[index],
					clk->reg_bits[index]);
		if (ret) {
			if (clk->reg_sel[old_index])
				ab8500_sysctrl_write(clk->reg_sel[old_index],
						clk->reg_mask[old_index],
						clk->reg_bits[old_index]);
			return ret;
		}
	}
	clk->parent_index = index;

	return ret;
}

static u8 clk_sysctrl_get_parent(struct clk_hw *hw)
{
	struct clk_sysctrl *clk = to_clk_sysctrl(hw);
	return clk->parent_index;
}

static struct clk_ops clk_sysctrl_gate_ops = {
	.prepare = clk_sysctrl_prepare,
	.unprepare = clk_sysctrl_unprepare,
};

static struct clk_ops clk_sysctrl_gate_fixed_rate_ops = {
	.prepare = clk_sysctrl_prepare,
	.unprepare = clk_sysctrl_unprepare,
	.recalc_rate = clk_sysctrl_recalc_rate,
};

static struct clk_ops clk_sysctrl_set_parent_ops = {
	.set_parent = clk_sysctrl_set_parent,
	.get_parent = clk_sysctrl_get_parent,
};

static struct clk *clk_reg_sysctrl(struct device *dev,
				const char *name,
				const char **parent_names,
				u8 num_parents,
				u16 *reg_sel,
				u8 *reg_mask,
				u8 *reg_bits,
				unsigned long rate,
				unsigned long enable_delay_us,
				unsigned long flags,
				struct clk_ops *clk_sysctrl_ops)
{
	struct clk_sysctrl *clk;
	struct clk_init_data clk_sysctrl_init;
	struct clk *clk_reg;
	int i;

	if (!dev)
		return ERR_PTR(-EINVAL);

	if (!name || (num_parents > SYSCTRL_MAX_NUM_PARENTS)) {
		dev_err(dev, "clk_sysctrl: invalid arguments passed\n");
		return ERR_PTR(-EINVAL);
	}

	clk = devm_kzalloc(dev, sizeof(struct clk_sysctrl), GFP_KERNEL);
	if (!clk) {
		dev_err(dev, "clk_sysctrl: could not allocate clk\n");
		return ERR_PTR(-ENOMEM);
	}

	/* set main clock registers */
	clk->reg_sel[0] = reg_sel[0];
	clk->reg_bits[0] = reg_bits[0];
	clk->reg_mask[0] = reg_mask[0];

	/* handle clocks with more than one parent */
	for (i = 1; i < num_parents; i++) {
		clk->reg_sel[i] = reg_sel[i];
		clk->reg_bits[i] = reg_bits[i];
		clk->reg_mask[i] = reg_mask[i];
	}

	clk->parent_index = 0;
	clk->rate = rate;
	clk->enable_delay_us = enable_delay_us;
	clk->dev = dev;

	clk_sysctrl_init.name = name;
	clk_sysctrl_init.ops = clk_sysctrl_ops;
	clk_sysctrl_init.flags = flags;
	clk_sysctrl_init.parent_names = parent_names;
	clk_sysctrl_init.num_parents = num_parents;
	clk->hw.init = &clk_sysctrl_init;

	clk_reg = devm_clk_register(clk->dev, &clk->hw);
	if (IS_ERR(clk_reg))
		dev_err(dev, "clk_sysctrl: clk_register failed\n");

	return clk_reg;
}

struct clk *clk_reg_sysctrl_gate(struct device *dev,
				const char *name,
				const char *parent_name,
				u16 reg_sel,
				u8 reg_mask,
				u8 reg_bits,
				unsigned long enable_delay_us,
				unsigned long flags)
{
	const char **parent_names = (parent_name ? &parent_name : NULL);
	u8 num_parents = (parent_name ? 1 : 0);

	return clk_reg_sysctrl(dev, name, parent_names, num_parents,
			&reg_sel, &reg_mask, &reg_bits, 0, enable_delay_us,
			flags, &clk_sysctrl_gate_ops);
}

struct clk *clk_reg_sysctrl_gate_fixed_rate(struct device *dev,
					const char *name,
					const char *parent_name,
					u16 reg_sel,
					u8 reg_mask,
					u8 reg_bits,
					unsigned long rate,
					unsigned long enable_delay_us,
					unsigned long flags)
{
	const char **parent_names = (parent_name ? &parent_name : NULL);
	u8 num_parents = (parent_name ? 1 : 0);

	return clk_reg_sysctrl(dev, name, parent_names, num_parents,
			&reg_sel, &reg_mask, &reg_bits,
			rate, enable_delay_us, flags,
			&clk_sysctrl_gate_fixed_rate_ops);
}

struct clk *clk_reg_sysctrl_set_parent(struct device *dev,
				const char *name,
				const char **parent_names,
				u8 num_parents,
				u16 *reg_sel,
				u8 *reg_mask,
				u8 *reg_bits,
				unsigned long flags)
{
	return clk_reg_sysctrl(dev, name, parent_names, num_parents,
			reg_sel, reg_mask, reg_bits, 0, 0, flags,
			&clk_sysctrl_set_parent_ops);
}
