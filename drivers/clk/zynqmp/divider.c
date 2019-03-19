// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC Divider support
 *
 *  Copyright (C) 2016-2018 Xilinx
 *
 * Adjustable divider clock implementation
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include "clk-zynqmp.h"

/*
 * DOC: basic adjustable divider clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.  clk->rate = ceiling(parent->rate / divisor)
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_zynqmp_clk_divider(_hw)		\
	container_of(_hw, struct zynqmp_clk_divider, hw)

#define CLK_FRAC	BIT(13) /* has a fractional parent */

/**
 * struct zynqmp_clk_divider - adjustable divider clock
 * @hw:		handle between common and hardware-specific interfaces
 * @flags:	Hardware specific flags
 * @is_frac:	The divider is a fractional divider
 * @clk_id:	Id of clock
 * @div_type:	divisor type (TYPE_DIV1 or TYPE_DIV2)
 */
struct zynqmp_clk_divider {
	struct clk_hw hw;
	u8 flags;
	bool is_frac;
	u32 clk_id;
	u32 div_type;
};

static inline int zynqmp_divider_get_val(unsigned long parent_rate,
					 unsigned long rate)
{
	return DIV_ROUND_CLOSEST(parent_rate, rate);
}

/**
 * zynqmp_clk_divider_recalc_rate() - Recalc rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @parent_rate:	rate of parent clock
 *
 * Return: 0 on success else error+reason
 */
static unsigned long zynqmp_clk_divider_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 div, value;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	ret = eemi_ops->clock_getdivider(clk_id, &div);

	if (ret)
		pr_warn_once("%s() get divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	if (div_type == TYPE_DIV1)
		value = div & 0xFFFF;
	else
		value = div >> 16;

	return DIV_ROUND_UP_ULL(parent_rate, value);
}

/**
 * zynqmp_clk_divider_round_rate() - Round rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @rate:		rate of clock to be set
 * @prate:		rate of parent clock
 *
 * Return: 0 on success else error+reason
 */
static long zynqmp_clk_divider_round_rate(struct clk_hw *hw,
					  unsigned long rate,
					  unsigned long *prate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 bestdiv;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		ret = eemi_ops->clock_getdivider(clk_id, &bestdiv);

		if (ret)
			pr_warn_once("%s() get divider failed for %s, ret = %d\n",
				     __func__, clk_name, ret);
		if (div_type == TYPE_DIV1)
			bestdiv = bestdiv & 0xFFFF;
		else
			bestdiv  = bestdiv >> 16;

		return DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	}

	bestdiv = zynqmp_divider_get_val(*prate, rate);

	if ((clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) && divider->is_frac)
		bestdiv = rate % *prate ? 1 : bestdiv;
	*prate = rate * bestdiv;

	return rate;
}

/**
 * zynqmp_clk_divider_set_rate() - Set rate of divider clock
 * @hw:			handle between common and hardware-specific interfaces
 * @rate:		rate of clock to be set
 * @parent_rate:	rate of parent clock
 *
 * Return: 0 on success else error+reason
 */
static int zynqmp_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct zynqmp_clk_divider *divider = to_zynqmp_clk_divider(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = divider->clk_id;
	u32 div_type = divider->div_type;
	u32 value, div;
	int ret;
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();

	value = zynqmp_divider_get_val(parent_rate, rate);
	if (div_type == TYPE_DIV1) {
		div = value & 0xFFFF;
		div |= 0xffff << 16;
	} else {
		div = 0xffff;
		div |= value << 16;
	}

	ret = eemi_ops->clock_setdivider(clk_id, div);

	if (ret)
		pr_warn_once("%s() set divider failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return ret;
}

static const struct clk_ops zynqmp_clk_divider_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
	.set_rate = zynqmp_clk_divider_set_rate,
};

/**
 * zynqmp_clk_register_divider() - Register a divider clock
 * @name:		Name of this clock
 * @clk_id:		Id of clock
 * @parents:		Name of this clock's parents
 * @num_parents:	Number of parents
 * @nodes:		Clock topology node
 *
 * Return: clock hardware to registered clock divider
 */
struct clk_hw *zynqmp_clk_register_divider(const char *name,
					   u32 clk_id,
					   const char * const *parents,
					   u8 num_parents,
					   const struct clock_topology *nodes)
{
	struct zynqmp_clk_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	/* allocate the divider */
	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_divider_ops;
	/* CLK_FRAC is not defined in the common clk framework */
	init.flags = nodes->flag & ~CLK_FRAC;
	init.parent_names = parents;
	init.num_parents = 1;

	/* struct clk_divider assignments */
	div->is_frac = !!(nodes->flag & CLK_FRAC);
	div->flags = nodes->type_flag;
	div->hw.init = &init;
	div->clk_id = clk_id;
	div->div_type = nodes->type;

	hw = &div->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(zynqmp_clk_register_divider);
