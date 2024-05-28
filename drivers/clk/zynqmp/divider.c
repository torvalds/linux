// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC Divider support
 *
 *  Copyright (C) 2016-2019 Xilinx
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

#define CLK_FRAC		BIT(13) /* has a fractional parent */
#define CUSTOM_FLAG_CLK_FRAC	BIT(0) /* has a fractional parent in custom type flag */

/**
 * struct zynqmp_clk_divider - adjustable divider clock
 * @hw:		handle between common and hardware-specific interfaces
 * @flags:	Hardware specific flags
 * @is_frac:	The divider is a fractional divider
 * @clk_id:	Id of clock
 * @div_type:	divisor type (TYPE_DIV1 or TYPE_DIV2)
 * @max_div:	maximum supported divisor (fetched from firmware)
 */
struct zynqmp_clk_divider {
	struct clk_hw hw;
	u8 flags;
	bool is_frac;
	u32 clk_id;
	u32 div_type;
	u16 max_div;
};

static inline int zynqmp_divider_get_val(unsigned long parent_rate,
					 unsigned long rate, u16 flags)
{
	int up, down;
	unsigned long up_rate, down_rate;

	if (flags & CLK_DIVIDER_POWER_OF_TWO) {
		up = DIV_ROUND_UP_ULL((u64)parent_rate, rate);
		down = DIV_ROUND_DOWN_ULL((u64)parent_rate, rate);

		up = __roundup_pow_of_two(up);
		down = __rounddown_pow_of_two(down);

		up_rate = DIV_ROUND_UP_ULL((u64)parent_rate, up);
		down_rate = DIV_ROUND_UP_ULL((u64)parent_rate, down);

		return (rate - up_rate) <= (down_rate - rate) ? up : down;

	} else {
		return DIV_ROUND_CLOSEST(parent_rate, rate);
	}
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

	ret = zynqmp_pm_clock_getdivider(clk_id, &div);

	if (ret)
		pr_debug("%s() get divider failed for %s, ret = %d\n",
			 __func__, clk_name, ret);

	if (div_type == TYPE_DIV1)
		value = div & 0xFFFF;
	else
		value = div >> 16;

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		value = 1 << value;

	if (!value) {
		WARN(!(divider->flags & CLK_DIVIDER_ALLOW_ZERO),
		     "%s: Zero divisor and CLK_DIVIDER_ALLOW_ZERO not set\n",
		     clk_name);
		return parent_rate;
	}

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
	u8 width;

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		ret = zynqmp_pm_clock_getdivider(clk_id, &bestdiv);

		if (ret)
			pr_debug("%s() get divider failed for %s, ret = %d\n",
				 __func__, clk_name, ret);
		if (div_type == TYPE_DIV1)
			bestdiv = bestdiv & 0xFFFF;
		else
			bestdiv  = bestdiv >> 16;

		if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
			bestdiv = 1 << bestdiv;

		return DIV_ROUND_UP_ULL((u64)*prate, bestdiv);
	}

	width = fls(divider->max_div);

	rate = divider_round_rate(hw, rate, prate, NULL, width, divider->flags);

	if (divider->is_frac && (clk_hw_get_flags(hw) & CLK_SET_RATE_PARENT) && (rate % *prate))
		*prate = rate;

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

	value = zynqmp_divider_get_val(parent_rate, rate, divider->flags);
	if (div_type == TYPE_DIV1) {
		div = value & 0xFFFF;
		div |= 0xffff << 16;
	} else {
		div = 0xffff;
		div |= value << 16;
	}

	if (divider->flags & CLK_DIVIDER_POWER_OF_TWO)
		div = __ffs(div);

	ret = zynqmp_pm_clock_setdivider(clk_id, div);

	if (ret)
		pr_debug("%s() set divider failed for %s, ret = %d\n",
			 __func__, clk_name, ret);

	return ret;
}

static const struct clk_ops zynqmp_clk_divider_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
	.set_rate = zynqmp_clk_divider_set_rate,
};

static const struct clk_ops zynqmp_clk_divider_ro_ops = {
	.recalc_rate = zynqmp_clk_divider_recalc_rate,
	.round_rate = zynqmp_clk_divider_round_rate,
};

/**
 * zynqmp_clk_get_max_divisor() - Get maximum supported divisor from firmware.
 * @clk_id:		Id of clock
 * @type:		Divider type
 *
 * Return: Maximum divisor of a clock if query data is successful
 *	   U16_MAX in case of query data is not success
 */
static u32 zynqmp_clk_get_max_divisor(u32 clk_id, u32 type)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_MAX_DIVISOR;
	qdata.arg1 = clk_id;
	qdata.arg2 = type;
	ret = zynqmp_pm_query_data(qdata, ret_payload);
	/*
	 * To maintain backward compatibility return maximum possible value
	 * (0xFFFF) if query for max divisor is not successful.
	 */
	if (ret)
		return U16_MAX;

	return ret_payload[1];
}

static inline unsigned long zynqmp_clk_map_divider_ccf_flags(
					       const u32 zynqmp_type_flag)
{
	unsigned long ccf_flag = 0;

	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_ONE_BASED)
		ccf_flag |= CLK_DIVIDER_ONE_BASED;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_POWER_OF_TWO)
		ccf_flag |= CLK_DIVIDER_POWER_OF_TWO;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_ALLOW_ZERO)
		ccf_flag |= CLK_DIVIDER_ALLOW_ZERO;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_POWER_OF_TWO)
		ccf_flag |= CLK_DIVIDER_HIWORD_MASK;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_ROUND_CLOSEST)
		ccf_flag |= CLK_DIVIDER_ROUND_CLOSEST;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_READ_ONLY)
		ccf_flag |= CLK_DIVIDER_READ_ONLY;
	if (zynqmp_type_flag & ZYNQMP_CLK_DIVIDER_MAX_AT_ZERO)
		ccf_flag |= CLK_DIVIDER_MAX_AT_ZERO;

	return ccf_flag;
}

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
	if (nodes->type_flag & CLK_DIVIDER_READ_ONLY)
		init.ops = &zynqmp_clk_divider_ro_ops;
	else
		init.ops = &zynqmp_clk_divider_ops;

	init.flags = zynqmp_clk_map_common_ccf_flags(nodes->flag);

	init.parent_names = parents;
	init.num_parents = 1;

	/* struct clk_divider assignments */
	div->is_frac = !!((nodes->flag & CLK_FRAC) |
			  (nodes->custom_type_flag & CUSTOM_FLAG_CLK_FRAC));
	div->flags = zynqmp_clk_map_divider_ccf_flags(nodes->type_flag);
	div->hw.init = &init;
	div->clk_id = clk_id;
	div->div_type = nodes->type;

	/*
	 * To achieve best possible rate, maximum limit of divider is required
	 * while computation.
	 */
	div->max_div = zynqmp_clk_get_max_divisor(clk_id, nodes->type);

	hw = &div->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw;
}
