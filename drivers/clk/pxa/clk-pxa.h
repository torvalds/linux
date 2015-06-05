/*
 * Marvell PXA family clocks
 *
 * Copyright (C) 2014 Robert Jarzmik
 *
 * Common clock code for PXA clocks ("CKEN" type clocks + DT)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */
#ifndef _CLK_PXA_
#define _CLK_PXA_

#define PARENTS(name) \
	static const char *name ## _parents[] __initdata
#define MUX_RO_RATE_RO_OPS(name, clk_name)			\
	static struct clk_hw name ## _mux_hw;			\
	static struct clk_hw name ## _rate_hw;			\
	static struct clk_ops name ## _mux_ops = {		\
		.get_parent = name ## _get_parent,		\
		.set_parent = dummy_clk_set_parent,		\
	};							\
	static struct clk_ops name ## _rate_ops = {		\
		.recalc_rate = name ## _get_rate,		\
	};							\
	static struct clk * __init clk_register_ ## name(void)	\
	{							\
		return clk_register_composite(NULL, clk_name,	\
			name ## _parents,			\
			ARRAY_SIZE(name ## _parents),		\
			&name ## _mux_hw, &name ## _mux_ops,	\
			&name ## _rate_hw, &name ## _rate_ops,	\
			NULL, NULL, CLK_GET_RATE_NOCACHE);	\
	}

#define RATE_RO_OPS(name, clk_name)			\
	static struct clk_hw name ## _rate_hw;			\
	static struct clk_ops name ## _rate_ops = {		\
		.recalc_rate = name ## _get_rate,		\
	};							\
	static struct clk * __init clk_register_ ## name(void)	\
	{							\
		return clk_register_composite(NULL, clk_name,	\
			name ## _parents,			\
			ARRAY_SIZE(name ## _parents),		\
			NULL, NULL,				\
			&name ## _rate_hw, &name ## _rate_ops,	\
			NULL, NULL, CLK_GET_RATE_NOCACHE);	\
	}

/*
 * CKEN clock type
 * This clock takes it source from 2 possible parents :
 *  - a low power parent
 *  - a normal parent
 *
 *  +------------+     +-----------+
 *  |  Low Power | --- | x mult_lp |
 *  |    Clock   |     | / div_lp  |\
 *  +------------+     +-----------+ \+-----+   +-----------+
 *                                    | Mux |---| CKEN gate |
 *  +------------+     +-----------+ /+-----+   +-----------+
 *  | High Power |     | x mult_hp |/
 *  |    Clock   | --- | / div_hp  |
 *  +------------+     +-----------+
 */
struct desc_clk_cken {
	struct clk_hw hw;
	int ckid;
	const char *name;
	const char *dev_id;
	const char *con_id;
	const char **parent_names;
	struct clk_fixed_factor lp;
	struct clk_fixed_factor hp;
	struct clk_gate gate;
	bool (*is_in_low_power)(void);
	const unsigned long flags;
};

#define PXA_CKEN(_dev_id, _con_id, _name, parents, _mult_lp, _div_lp,	\
		 _mult_hp, _div_hp, is_lp, _cken_reg, _cken_bit, flag)	\
	{ .ckid = CLK_ ## _name, .name = #_name,			\
	  .dev_id = _dev_id, .con_id = _con_id,	.parent_names = parents,\
	  .lp = { .mult = _mult_lp, .div = _div_lp },			\
	  .hp = { .mult = _mult_hp, .div = _div_hp },			\
	  .is_in_low_power = is_lp,					\
	  .gate = { .reg = (void __iomem *)_cken_reg, .bit_idx = _cken_bit }, \
	  .flags = flag,						\
	}
#define PXA_CKEN_1RATE(dev_id, con_id, name, parents, cken_reg,		\
			    cken_bit, flag)				\
	PXA_CKEN(dev_id, con_id, name, parents, 1, 1, 1, 1,		\
		 NULL, cken_reg, cken_bit, flag)

static int dummy_clk_set_parent(struct clk_hw *hw, u8 index)
{
	return 0;
}

extern void clkdev_pxa_register(int ckid, const char *con_id,
				const char *dev_id, struct clk *clk);
extern int clk_pxa_cken_init(const struct desc_clk_cken *clks, int nb_clks);
void clk_pxa_dt_common_init(struct device_node *np);

#endif
