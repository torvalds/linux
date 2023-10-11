// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Renesas Versaclock 3
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#include <linux/clk-provider.h>
#include <linux/i2c.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define NUM_CONFIG_REGISTERS		37

#define VC3_GENERAL_CTR			0x0
#define VC3_GENERAL_CTR_DIV1_SRC_SEL	BIT(3)
#define VC3_GENERAL_CTR_PLL3_REFIN_SEL	BIT(2)

#define VC3_PLL3_M_DIVIDER		0x3
#define VC3_PLL3_M_DIV1			BIT(7)
#define VC3_PLL3_M_DIV2			BIT(6)
#define VC3_PLL3_M_DIV(n)		((n) & GENMASK(5, 0))

#define VC3_PLL3_N_DIVIDER		0x4
#define VC3_PLL3_LOOP_FILTER_N_DIV_MSB	0x5

#define VC3_PLL3_CHARGE_PUMP_CTRL	0x6
#define VC3_PLL3_CHARGE_PUMP_CTRL_OUTDIV3_SRC_SEL	BIT(7)

#define VC3_PLL1_CTRL_OUTDIV5		0x7
#define VC3_PLL1_CTRL_OUTDIV5_PLL1_MDIV_DOUBLER		BIT(7)

#define VC3_PLL1_M_DIVIDER		0x8
#define VC3_PLL1_M_DIV1			BIT(7)
#define VC3_PLL1_M_DIV2			BIT(6)
#define VC3_PLL1_M_DIV(n)		((n) & GENMASK(5, 0))

#define VC3_PLL1_VCO_N_DIVIDER		0x9
#define VC3_PLL1_LOOP_FILTER_N_DIV_MSB	0x0a

#define VC3_OUT_DIV1_DIV2_CTRL		0xf

#define VC3_PLL2_FB_INT_DIV_MSB		0x10
#define VC3_PLL2_FB_INT_DIV_LSB		0x11
#define VC3_PLL2_FB_FRC_DIV_MSB		0x12
#define VC3_PLL2_FB_FRC_DIV_LSB		0x13

#define VC3_PLL2_M_DIVIDER		0x1a
#define VC3_PLL2_MDIV_DOUBLER		BIT(7)
#define VC3_PLL2_M_DIV1			BIT(6)
#define VC3_PLL2_M_DIV2			BIT(5)
#define VC3_PLL2_M_DIV(n)		((n) & GENMASK(4, 0))

#define VC3_OUT_DIV3_DIV4_CTRL		0x1b

#define VC3_PLL_OP_CTRL			0x1c
#define VC3_PLL_OP_CTRL_PLL2_REFIN_SEL	6

#define VC3_OUTPUT_CTR			0x1d
#define VC3_OUTPUT_CTR_DIV4_SRC_SEL	BIT(3)

#define VC3_SE2_CTRL_REG0		0x1f
#define VC3_SE2_CTRL_REG0_SE2_CLK_SEL	BIT(6)

#define VC3_SE3_DIFF1_CTRL_REG		0x21
#define VC3_SE3_DIFF1_CTRL_REG_SE3_CLK_SEL	BIT(6)

#define VC3_DIFF1_CTRL_REG		0x22
#define VC3_DIFF1_CTRL_REG_DIFF1_CLK_SEL	BIT(7)

#define VC3_DIFF2_CTRL_REG		0x23
#define VC3_DIFF2_CTRL_REG_DIFF2_CLK_SEL	BIT(7)

#define VC3_SE1_DIV4_CTRL		0x24
#define VC3_SE1_DIV4_CTRL_SE1_CLK_SEL	BIT(3)

#define VC3_PLL1_VCO_MIN		300000000UL
#define VC3_PLL1_VCO_MAX		600000000UL

#define VC3_PLL2_VCO_MIN		400000000UL
#define VC3_PLL2_VCO_MAX		1200000000UL

#define VC3_PLL3_VCO_MIN		300000000UL
#define VC3_PLL3_VCO_MAX		800000000UL

#define VC3_2_POW_16			(U16_MAX + 1)
#define VC3_DIV_MASK(width)		((1 << (width)) - 1)

enum vc3_pfd_mux {
	VC3_PFD2_MUX,
	VC3_PFD3_MUX,
};

enum vc3_pfd {
	VC3_PFD1,
	VC3_PFD2,
	VC3_PFD3,
};

enum vc3_pll {
	VC3_PLL1,
	VC3_PLL2,
	VC3_PLL3,
};

enum vc3_div_mux {
	VC3_DIV1_MUX,
	VC3_DIV3_MUX,
	VC3_DIV4_MUX,
};

enum vc3_div {
	VC3_DIV1,
	VC3_DIV2,
	VC3_DIV3,
	VC3_DIV4,
	VC3_DIV5,
};

enum vc3_clk_mux {
	VC3_DIFF2_MUX,
	VC3_DIFF1_MUX,
	VC3_SE3_MUX,
	VC3_SE2_MUX,
	VC3_SE1_MUX,
};

enum vc3_clk {
	VC3_DIFF2,
	VC3_DIFF1,
	VC3_SE3,
	VC3_SE2,
	VC3_SE1,
	VC3_REF,
};

struct vc3_clk_data {
	u8 offs;
	u8 bitmsk;
};

struct vc3_pfd_data {
	u8 num;
	u8 offs;
	u8 mdiv1_bitmsk;
	u8 mdiv2_bitmsk;
};

struct vc3_pll_data {
	u8 num;
	u8 int_div_msb_offs;
	u8 int_div_lsb_offs;
	unsigned long vco_min;
	unsigned long vco_max;
};

struct vc3_div_data {
	u8 offs;
	const struct clk_div_table *table;
	u8 shift;
	u8 width;
	u8 flags;
};

struct vc3_hw_data {
	struct clk_hw hw;
	struct regmap *regmap;
	const void *data;

	u32 div_int;
	u32 div_frc;
};

static const struct clk_div_table div1_divs[] = {
	{ .val = 0, .div = 1, }, { .val = 1, .div = 4, },
	{ .val = 2, .div = 5, }, { .val = 3, .div = 6, },
	{ .val = 4, .div = 2, }, { .val = 5, .div = 8, },
	{ .val = 6, .div = 10, }, { .val = 7, .div = 12, },
	{ .val = 8, .div = 4, }, { .val = 9, .div = 16, },
	{ .val = 10, .div = 20, }, { .val = 11, .div = 24, },
	{ .val = 12, .div = 8, }, { .val = 13, .div = 32, },
	{ .val = 14, .div = 40, }, { .val = 15, .div = 48, },
	{}
};

static const struct clk_div_table div245_divs[] = {
	{ .val = 0, .div = 1, }, { .val = 1, .div = 3, },
	{ .val = 2, .div = 5, }, { .val = 3, .div = 10, },
	{ .val = 4, .div = 2, }, { .val = 5, .div = 6, },
	{ .val = 6, .div = 10, }, { .val = 7, .div = 20, },
	{ .val = 8, .div = 4, }, { .val = 9, .div = 12, },
	{ .val = 10, .div = 20, }, { .val = 11, .div = 40, },
	{ .val = 12, .div = 5, }, { .val = 13, .div = 15, },
	{ .val = 14, .div = 25, }, { .val = 15, .div = 50, },
	{}
};

static const struct clk_div_table div3_divs[] = {
	{ .val = 0, .div = 1, }, { .val = 1, .div = 3, },
	{ .val = 2, .div = 5, }, { .val = 3, .div = 10, },
	{ .val = 4, .div = 2, }, { .val = 5, .div = 6, },
	{ .val = 6, .div = 10, }, { .val = 7, .div = 20, },
	{ .val = 8, .div = 4, }, { .val = 9, .div = 12, },
	{ .val = 10, .div = 20, }, { .val = 11, .div = 40, },
	{ .val = 12, .div = 8, }, { .val = 13, .div = 24, },
	{ .val = 14, .div = 40, }, { .val = 15, .div = 80, },
	{}
};

static struct clk_hw *clk_out[6];

static unsigned char vc3_pfd_mux_get_parent(struct clk_hw *hw)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *pfd_mux = vc3->data;
	u32 src;

	regmap_read(vc3->regmap, pfd_mux->offs, &src);

	return !!(src & pfd_mux->bitmsk);
}

static int vc3_pfd_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *pfd_mux = vc3->data;

	regmap_update_bits(vc3->regmap, pfd_mux->offs, pfd_mux->bitmsk,
			   index ? pfd_mux->bitmsk : 0);
	return 0;
}

static const struct clk_ops vc3_pfd_mux_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.set_parent = vc3_pfd_mux_set_parent,
	.get_parent = vc3_pfd_mux_get_parent,
};

static unsigned long vc3_pfd_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pfd_data *pfd = vc3->data;
	unsigned int prediv, premul;
	unsigned long rate;
	u8 mdiv;

	regmap_read(vc3->regmap, pfd->offs, &prediv);
	if (pfd->num == VC3_PFD1) {
		/* The bypass_prediv is set, PLL fed from Ref_in directly. */
		if (prediv & pfd->mdiv1_bitmsk) {
			/* check doubler is set or not */
			regmap_read(vc3->regmap, VC3_PLL1_CTRL_OUTDIV5, &premul);
			if (premul & VC3_PLL1_CTRL_OUTDIV5_PLL1_MDIV_DOUBLER)
				parent_rate *= 2;
			return parent_rate;
		}
		mdiv = VC3_PLL1_M_DIV(prediv);
	} else if (pfd->num == VC3_PFD2) {
		/* The bypass_prediv is set, PLL fed from Ref_in directly. */
		if (prediv & pfd->mdiv1_bitmsk) {
			regmap_read(vc3->regmap, VC3_PLL2_M_DIVIDER, &premul);
			/* check doubler is set or not */
			if (premul & VC3_PLL2_MDIV_DOUBLER)
				parent_rate *= 2;
			return parent_rate;
		}

		mdiv = VC3_PLL2_M_DIV(prediv);
	} else {
		/* The bypass_prediv is set, PLL fed from Ref_in directly. */
		if (prediv & pfd->mdiv1_bitmsk)
			return parent_rate;

		mdiv = VC3_PLL3_M_DIV(prediv);
	}

	if (prediv & pfd->mdiv2_bitmsk)
		rate = parent_rate / 2;
	else
		rate = parent_rate / mdiv;

	return rate;
}

static long vc3_pfd_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pfd_data *pfd = vc3->data;
	unsigned long idiv;

	/* PLL cannot operate with input clock above 50 MHz. */
	if (rate > 50000000)
		return -EINVAL;

	/* CLKIN within range of PLL input, feed directly to PLL. */
	if (*parent_rate <= 50000000)
		return *parent_rate;

	idiv = DIV_ROUND_UP(*parent_rate, rate);
	if (pfd->num == VC3_PFD1 || pfd->num == VC3_PFD3) {
		if (idiv > 63)
			return -EINVAL;
	} else {
		if (idiv > 31)
			return -EINVAL;
	}

	return *parent_rate / idiv;
}

static int vc3_pfd_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pfd_data *pfd = vc3->data;
	unsigned long idiv;
	u8 div;

	/* CLKIN within range of PLL input, feed directly to PLL. */
	if (parent_rate <= 50000000) {
		regmap_update_bits(vc3->regmap, pfd->offs, pfd->mdiv1_bitmsk,
				   pfd->mdiv1_bitmsk);
		regmap_update_bits(vc3->regmap, pfd->offs, pfd->mdiv2_bitmsk, 0);
		return 0;
	}

	idiv = DIV_ROUND_UP(parent_rate, rate);
	/* We have dedicated div-2 predivider. */
	if (idiv == 2) {
		regmap_update_bits(vc3->regmap, pfd->offs, pfd->mdiv2_bitmsk,
				   pfd->mdiv2_bitmsk);
		regmap_update_bits(vc3->regmap, pfd->offs, pfd->mdiv1_bitmsk, 0);
	} else {
		if (pfd->num == VC3_PFD1)
			div = VC3_PLL1_M_DIV(idiv);
		else if (pfd->num == VC3_PFD2)
			div = VC3_PLL2_M_DIV(idiv);
		else
			div = VC3_PLL3_M_DIV(idiv);

		regmap_write(vc3->regmap, pfd->offs, div);
	}

	return 0;
}

static const struct clk_ops vc3_pfd_ops = {
	.recalc_rate = vc3_pfd_recalc_rate,
	.round_rate = vc3_pfd_round_rate,
	.set_rate = vc3_pfd_set_rate,
};

static unsigned long vc3_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pll_data *pll = vc3->data;
	u32 div_int, div_frc, val;
	unsigned long rate;

	regmap_read(vc3->regmap, pll->int_div_msb_offs, &val);
	div_int = (val & GENMASK(2, 0)) << 8;
	regmap_read(vc3->regmap, pll->int_div_lsb_offs, &val);
	div_int |= val;

	if (pll->num == VC3_PLL2) {
		regmap_read(vc3->regmap, VC3_PLL2_FB_FRC_DIV_MSB, &val);
		div_frc = val << 8;
		regmap_read(vc3->regmap, VC3_PLL2_FB_FRC_DIV_LSB, &val);
		div_frc |= val;
		rate = (parent_rate *
			(div_int * VC3_2_POW_16 + div_frc) / VC3_2_POW_16);
	} else {
		rate = parent_rate * div_int;
	}

	return rate;
}

static long vc3_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pll_data *pll = vc3->data;
	u64 div_frc;

	if (rate < pll->vco_min)
		rate = pll->vco_min;
	if (rate > pll->vco_max)
		rate = pll->vco_max;

	vc3->div_int = rate / *parent_rate;

	if (pll->num == VC3_PLL2) {
		if (vc3->div_int > 0x7ff)
			rate = *parent_rate * 0x7ff;

		/* Determine best fractional part, which is 16 bit wide */
		div_frc = rate % *parent_rate;
		div_frc *= BIT(16) - 1;
		do_div(div_frc, *parent_rate);

		vc3->div_frc = (u32)div_frc;
		rate = (*parent_rate *
			(vc3->div_int * VC3_2_POW_16 + div_frc) / VC3_2_POW_16);
	} else {
		rate = *parent_rate * vc3->div_int;
	}

	return rate;
}

static int vc3_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_pll_data *pll = vc3->data;
	u32 val;

	regmap_read(vc3->regmap, pll->int_div_msb_offs, &val);
	val = (val & 0xf8) | ((vc3->div_int >> 8) & 0x7);
	regmap_write(vc3->regmap, pll->int_div_msb_offs, val);
	regmap_write(vc3->regmap, pll->int_div_lsb_offs, vc3->div_int & 0xff);

	if (pll->num == VC3_PLL2) {
		regmap_write(vc3->regmap, VC3_PLL2_FB_FRC_DIV_MSB,
			     vc3->div_frc >> 8);
		regmap_write(vc3->regmap, VC3_PLL2_FB_FRC_DIV_LSB,
			     vc3->div_frc & 0xff);
	}

	return 0;
}

static const struct clk_ops vc3_pll_ops = {
	.recalc_rate = vc3_pll_recalc_rate,
	.round_rate = vc3_pll_round_rate,
	.set_rate = vc3_pll_set_rate,
};

static unsigned char vc3_div_mux_get_parent(struct clk_hw *hw)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *div_mux = vc3->data;
	u32 src;

	regmap_read(vc3->regmap, div_mux->offs, &src);

	return !!(src & div_mux->bitmsk);
}

static int vc3_div_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *div_mux = vc3->data;

	regmap_update_bits(vc3->regmap, div_mux->offs, div_mux->bitmsk,
			   index ? div_mux->bitmsk : 0);

	return 0;
}

static const struct clk_ops vc3_div_mux_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.set_parent = vc3_div_mux_set_parent,
	.get_parent = vc3_div_mux_get_parent,
};

static unsigned int vc3_get_div(const struct clk_div_table *table,
				unsigned int val, unsigned long flag)
{
	const struct clk_div_table *clkt;

	for (clkt = table; clkt->div; clkt++)
		if (clkt->val == val)
			return clkt->div;

	return 0;
}

static unsigned long vc3_div_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_div_data *div_data = vc3->data;
	unsigned int val;

	regmap_read(vc3->regmap, div_data->offs, &val);
	val >>= div_data->shift;
	val &= VC3_DIV_MASK(div_data->width);

	return divider_recalc_rate(hw, parent_rate, val, div_data->table,
				   div_data->flags, div_data->width);
}

static long vc3_div_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_div_data *div_data = vc3->data;
	unsigned int bestdiv;

	/* if read only, just return current value */
	if (div_data->flags & CLK_DIVIDER_READ_ONLY) {
		regmap_read(vc3->regmap, div_data->offs, &bestdiv);
		bestdiv >>= div_data->shift;
		bestdiv &= VC3_DIV_MASK(div_data->width);
		bestdiv = vc3_get_div(div_data->table, bestdiv, div_data->flags);
		return DIV_ROUND_UP(*parent_rate, bestdiv);
	}

	return divider_round_rate(hw, rate, parent_rate, div_data->table,
				  div_data->width, div_data->flags);
}

static int vc3_div_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_div_data *div_data = vc3->data;
	unsigned int value;

	value = divider_get_val(rate, parent_rate, div_data->table,
				div_data->width, div_data->flags);
	regmap_update_bits(vc3->regmap, div_data->offs,
			   VC3_DIV_MASK(div_data->width) << div_data->shift,
			   value << div_data->shift);
	return 0;
}

static const struct clk_ops vc3_div_ops = {
	.recalc_rate = vc3_div_recalc_rate,
	.round_rate = vc3_div_round_rate,
	.set_rate = vc3_div_set_rate,
};

static int vc3_clk_mux_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	int ret;
	int frc;

	ret = clk_mux_determine_rate_flags(hw, req, CLK_SET_RATE_PARENT);
	if (ret) {
		/* The below check is equivalent to (best_parent_rate/rate) */
		if (req->best_parent_rate >= req->rate) {
			frc = DIV_ROUND_CLOSEST_ULL(req->best_parent_rate,
						    req->rate);
			req->rate *= frc;
			return clk_mux_determine_rate_flags(hw, req,
							    CLK_SET_RATE_PARENT);
		}
		ret = 0;
	}

	return ret;
}

static unsigned char vc3_clk_mux_get_parent(struct clk_hw *hw)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *clk_mux = vc3->data;
	u32 val;

	regmap_read(vc3->regmap, clk_mux->offs, &val);

	return !!(val & clk_mux->bitmsk);
}

static int vc3_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct vc3_hw_data *vc3 = container_of(hw, struct vc3_hw_data, hw);
	const struct vc3_clk_data *clk_mux = vc3->data;

	regmap_update_bits(vc3->regmap, clk_mux->offs,
			   clk_mux->bitmsk, index ? clk_mux->bitmsk : 0);
	return 0;
}

static const struct clk_ops vc3_clk_mux_ops = {
	.determine_rate = vc3_clk_mux_determine_rate,
	.set_parent = vc3_clk_mux_set_parent,
	.get_parent = vc3_clk_mux_get_parent,
};

static bool vc3_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config vc3_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0x24,
	.writeable_reg = vc3_regmap_is_writeable,
};

static struct vc3_hw_data clk_div[5];

static const struct clk_parent_data pfd_mux_parent_data[] = {
	{ .index = 0, },
	{ .hw = &clk_div[VC3_DIV2].hw }
};

static struct vc3_hw_data clk_pfd_mux[] = {
	[VC3_PFD2_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_PLL_OP_CTRL,
			.bitmsk = BIT(VC3_PLL_OP_CTRL_PLL2_REFIN_SEL)
		},
		.hw.init = &(struct clk_init_data){
			.name = "pfd2_mux",
			.ops = &vc3_pfd_mux_ops,
			.parent_data = pfd_mux_parent_data,
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT
		}
	},
	[VC3_PFD3_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_GENERAL_CTR,
			.bitmsk = BIT(VC3_GENERAL_CTR_PLL3_REFIN_SEL)
		},
		.hw.init = &(struct clk_init_data){
			.name = "pfd3_mux",
			.ops = &vc3_pfd_mux_ops,
			.parent_data = pfd_mux_parent_data,
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT
		}
	}
};

static struct vc3_hw_data clk_pfd[] = {
	[VC3_PFD1] = {
		.data = &(struct vc3_pfd_data) {
			.num = VC3_PFD1,
			.offs = VC3_PLL1_M_DIVIDER,
			.mdiv1_bitmsk = VC3_PLL1_M_DIV1,
			.mdiv2_bitmsk = VC3_PLL1_M_DIV2
		},
		.hw.init = &(struct clk_init_data){
			.name = "pfd1",
			.ops = &vc3_pfd_ops,
			.parent_data = &(const struct clk_parent_data) {
				.index = 0
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_PFD2] = {
		.data = &(struct vc3_pfd_data) {
			.num = VC3_PFD2,
			.offs = VC3_PLL2_M_DIVIDER,
			.mdiv1_bitmsk = VC3_PLL2_M_DIV1,
			.mdiv2_bitmsk = VC3_PLL2_M_DIV2
		},
		.hw.init = &(struct clk_init_data){
			.name = "pfd2",
			.ops = &vc3_pfd_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pfd_mux[VC3_PFD2_MUX].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_PFD3] = {
		.data = &(struct vc3_pfd_data) {
			.num = VC3_PFD3,
			.offs = VC3_PLL3_M_DIVIDER,
			.mdiv1_bitmsk = VC3_PLL3_M_DIV1,
			.mdiv2_bitmsk = VC3_PLL3_M_DIV2
		},
		.hw.init = &(struct clk_init_data){
			.name = "pfd3",
			.ops = &vc3_pfd_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pfd_mux[VC3_PFD3_MUX].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	}
};

static struct vc3_hw_data clk_pll[] = {
	[VC3_PLL1] = {
		.data = &(struct vc3_pll_data) {
			.num = VC3_PLL1,
			.int_div_msb_offs = VC3_PLL1_LOOP_FILTER_N_DIV_MSB,
			.int_div_lsb_offs = VC3_PLL1_VCO_N_DIVIDER,
			.vco_min = VC3_PLL1_VCO_MIN,
			.vco_max = VC3_PLL1_VCO_MAX
		},
		.hw.init = &(struct clk_init_data){
			.name = "pll1",
			.ops = &vc3_pll_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pfd[VC3_PFD1].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_PLL2] = {
		.data = &(struct vc3_pll_data) {
			.num = VC3_PLL2,
			.int_div_msb_offs = VC3_PLL2_FB_INT_DIV_MSB,
			.int_div_lsb_offs = VC3_PLL2_FB_INT_DIV_LSB,
			.vco_min = VC3_PLL2_VCO_MIN,
			.vco_max = VC3_PLL2_VCO_MAX
		},
		.hw.init = &(struct clk_init_data){
			.name = "pll2",
			.ops = &vc3_pll_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pfd[VC3_PFD2].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_PLL3] = {
		.data = &(struct vc3_pll_data) {
			.num = VC3_PLL3,
			.int_div_msb_offs = VC3_PLL3_LOOP_FILTER_N_DIV_MSB,
			.int_div_lsb_offs = VC3_PLL3_N_DIVIDER,
			.vco_min = VC3_PLL3_VCO_MIN,
			.vco_max = VC3_PLL3_VCO_MAX
		},
		.hw.init = &(struct clk_init_data){
			.name = "pll3",
			.ops = &vc3_pll_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pfd[VC3_PFD3].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	}
};

static const struct clk_parent_data div_mux_parent_data[][2] = {
	[VC3_DIV1_MUX] = {
		{ .hw = &clk_pll[VC3_PLL1].hw },
		{ .index = 0 }
	},
	[VC3_DIV3_MUX] = {
		{ .hw = &clk_pll[VC3_PLL2].hw },
		{ .hw = &clk_pll[VC3_PLL3].hw }
	},
	[VC3_DIV4_MUX] = {
		{ .hw = &clk_pll[VC3_PLL2].hw },
		{ .index = 0 }
	}
};

static struct vc3_hw_data clk_div_mux[] = {
	[VC3_DIV1_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_GENERAL_CTR,
			.bitmsk = VC3_GENERAL_CTR_DIV1_SRC_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "div1_mux",
			.ops = &vc3_div_mux_ops,
			.parent_data = div_mux_parent_data[VC3_DIV1_MUX],
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT
		}
	},
	[VC3_DIV3_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_PLL3_CHARGE_PUMP_CTRL,
			.bitmsk = VC3_PLL3_CHARGE_PUMP_CTRL_OUTDIV3_SRC_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "div3_mux",
			.ops = &vc3_div_mux_ops,
			.parent_data = div_mux_parent_data[VC3_DIV3_MUX],
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT
		}
	},
	[VC3_DIV4_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_OUTPUT_CTR,
			.bitmsk = VC3_OUTPUT_CTR_DIV4_SRC_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "div4_mux",
			.ops = &vc3_div_mux_ops,
			.parent_data = div_mux_parent_data[VC3_DIV4_MUX],
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT
		}
	}
};

static struct vc3_hw_data clk_div[] = {
	[VC3_DIV1] = {
		.data = &(struct vc3_div_data) {
			.offs = VC3_OUT_DIV1_DIV2_CTRL,
			.table = div1_divs,
			.shift = 4,
			.width = 4,
			.flags = CLK_DIVIDER_READ_ONLY
		},
		.hw.init = &(struct clk_init_data){
			.name = "div1",
			.ops = &vc3_div_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div_mux[VC3_DIV1_MUX].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_DIV2] = {
		.data = &(struct vc3_div_data) {
			.offs = VC3_OUT_DIV1_DIV2_CTRL,
			.table = div245_divs,
			.shift = 0,
			.width = 4,
			.flags = CLK_DIVIDER_READ_ONLY
		},
		.hw.init = &(struct clk_init_data){
			.name = "div2",
			.ops = &vc3_div_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pll[VC3_PLL1].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_DIV3] = {
		.data = &(struct vc3_div_data) {
			.offs = VC3_OUT_DIV3_DIV4_CTRL,
			.table = div3_divs,
			.shift = 4,
			.width = 4,
			.flags = CLK_DIVIDER_READ_ONLY
		},
		.hw.init = &(struct clk_init_data){
			.name = "div3",
			.ops = &vc3_div_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div_mux[VC3_DIV3_MUX].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_DIV4] = {
		.data = &(struct vc3_div_data) {
			.offs = VC3_OUT_DIV3_DIV4_CTRL,
			.table = div245_divs,
			.shift = 0,
			.width = 4,
			.flags = CLK_DIVIDER_READ_ONLY
		},
		.hw.init = &(struct clk_init_data){
			.name = "div4",
			.ops = &vc3_div_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div_mux[VC3_DIV4_MUX].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_DIV5] = {
		.data = &(struct vc3_div_data) {
			.offs = VC3_PLL1_CTRL_OUTDIV5,
			.table = div245_divs,
			.shift = 0,
			.width = 4,
			.flags = CLK_DIVIDER_READ_ONLY
		},
		.hw.init = &(struct clk_init_data){
			.name = "div5",
			.ops = &vc3_div_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_pll[VC3_PLL3].hw
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT
		}
	}
};

static struct vc3_hw_data clk_mux[] = {
	[VC3_DIFF2_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_DIFF2_CTRL_REG,
			.bitmsk = VC3_DIFF2_CTRL_REG_DIFF2_CLK_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "diff2_mux",
			.ops = &vc3_clk_mux_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div[VC3_DIV1].hw,
				&clk_div[VC3_DIV3].hw
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_DIFF1_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_DIFF1_CTRL_REG,
			.bitmsk = VC3_DIFF1_CTRL_REG_DIFF1_CLK_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "diff1_mux",
			.ops = &vc3_clk_mux_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div[VC3_DIV1].hw,
				&clk_div[VC3_DIV3].hw
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_SE3_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_SE3_DIFF1_CTRL_REG,
			.bitmsk = VC3_SE3_DIFF1_CTRL_REG_SE3_CLK_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "se3_mux",
			.ops = &vc3_clk_mux_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div[VC3_DIV2].hw,
				&clk_div[VC3_DIV4].hw
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_SE2_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_SE2_CTRL_REG0,
			.bitmsk = VC3_SE2_CTRL_REG0_SE2_CLK_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "se2_mux",
			.ops = &vc3_clk_mux_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div[VC3_DIV5].hw,
				&clk_div[VC3_DIV4].hw
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT
		}
	},
	[VC3_SE1_MUX] = {
		.data = &(struct vc3_clk_data) {
			.offs = VC3_SE1_DIV4_CTRL,
			.bitmsk = VC3_SE1_DIV4_CTRL_SE1_CLK_SEL
		},
		.hw.init = &(struct clk_init_data){
			.name = "se1_mux",
			.ops = &vc3_clk_mux_ops,
			.parent_hws = (const struct clk_hw *[]) {
				&clk_div[VC3_DIV5].hw,
				&clk_div[VC3_DIV4].hw
			},
			.num_parents = 2,
			.flags = CLK_SET_RATE_PARENT
		}
	}
};

static struct clk_hw *vc3_of_clk_get(struct of_phandle_args *clkspec,
				     void *data)
{
	unsigned int idx = clkspec->args[0];
	struct clk_hw **clkout_hw = data;

	if (idx >= ARRAY_SIZE(clk_out)) {
		pr_err("invalid clk index %u for provider %pOF\n", idx, clkspec->np);
		return ERR_PTR(-EINVAL);
	}

	return clkout_hw[idx];
}

static int vc3_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	u8 settings[NUM_CONFIG_REGISTERS];
	struct regmap *regmap;
	const char *name;
	int ret, i;

	regmap = devm_regmap_init_i2c(client, &vc3_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "failed to allocate register map\n");

	ret = of_property_read_u8_array(dev->of_node, "renesas,settings",
					settings, ARRAY_SIZE(settings));
	if (!ret) {
		/*
		 * A raw settings array was specified in the DT. Write the
		 * settings to the device immediately.
		 */
		for  (i = 0; i < NUM_CONFIG_REGISTERS; i++) {
			ret = regmap_write(regmap, i, settings[i]);
			if (ret) {
				dev_err(dev, "error writing to chip (%i)\n", ret);
				return ret;
			}
		}
	} else if (ret == -EOVERFLOW) {
		dev_err(&client->dev, "EOVERFLOW reg settings. ARRAY_SIZE: %zu\n",
			ARRAY_SIZE(settings));
		return ret;
	}

	/* Register pfd muxes */
	for (i = 0; i < ARRAY_SIZE(clk_pfd_mux); i++) {
		clk_pfd_mux[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_pfd_mux[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_pfd_mux[i].hw.init->name);
	}

	/* Register pfd's */
	for (i = 0; i < ARRAY_SIZE(clk_pfd); i++) {
		clk_pfd[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_pfd[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_pfd[i].hw.init->name);
	}

	/* Register pll's */
	for (i = 0; i < ARRAY_SIZE(clk_pll); i++) {
		clk_pll[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_pll[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_pll[i].hw.init->name);
	}

	/* Register divider muxes */
	for (i = 0; i < ARRAY_SIZE(clk_div_mux); i++) {
		clk_div_mux[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_div_mux[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_div_mux[i].hw.init->name);
	}

	/* Register dividers */
	for (i = 0; i < ARRAY_SIZE(clk_div); i++) {
		clk_div[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_div[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_div[i].hw.init->name);
	}

	/* Register clk muxes */
	for (i = 0; i < ARRAY_SIZE(clk_mux); i++) {
		clk_mux[i].regmap = regmap;
		ret = devm_clk_hw_register(dev, &clk_mux[i].hw);
		if (ret)
			return dev_err_probe(dev, ret, "%s failed\n",
					     clk_mux[i].hw.init->name);
	}

	/* Register clk outputs */
	for (i = 0; i < ARRAY_SIZE(clk_out); i++) {
		switch (i) {
		case VC3_DIFF2:
			name = "diff2";
			break;
		case VC3_DIFF1:
			name = "diff1";
			break;
		case VC3_SE3:
			name = "se3";
			break;
		case VC3_SE2:
			name = "se2";
			break;
		case VC3_SE1:
			name = "se1";
			break;
		case VC3_REF:
			name = "ref";
			break;
		default:
			return dev_err_probe(dev, -EINVAL, "invalid clk output %d\n", i);
		}

		if (i == VC3_REF)
			clk_out[i] = devm_clk_hw_register_fixed_factor_index(dev,
				name, 0, CLK_SET_RATE_PARENT, 1, 1);
		else
			clk_out[i] = devm_clk_hw_register_fixed_factor_parent_hw(dev,
				name, &clk_mux[i].hw, CLK_SET_RATE_PARENT, 1, 1);

		if (IS_ERR(clk_out[i]))
			return PTR_ERR(clk_out[i]);
	}

	ret = devm_of_clk_add_hw_provider(dev, vc3_of_clk_get, clk_out);
	if (ret)
		return dev_err_probe(dev, ret, "unable to add clk provider\n");

	return ret;
}

static const struct of_device_id dev_ids[] = {
	{ .compatible = "renesas,5p35023" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, dev_ids);

static struct i2c_driver vc3_driver = {
	.driver = {
		.name = "vc3",
		.of_match_table = of_match_ptr(dev_ids),
	},
	.probe = vc3_probe,
};
module_i2c_driver(vc3_driver);

MODULE_AUTHOR("Biju Das <biju.das.jz@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas VersaClock 3 driver");
MODULE_LICENSE("GPL");
