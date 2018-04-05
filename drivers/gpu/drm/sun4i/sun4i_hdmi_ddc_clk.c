/*
 * Copyright (C) 2016 Free Electrons
 * Copyright (C) 2016 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "sun4i_hdmi_i2c_drv.h"

struct sun4i_ddc {
	struct clk_hw		hw;
	struct regmap_field	*reg;
	struct clk		*parent_clk;
	u8			pre_div;
	u8			m_offset;
};

static inline struct sun4i_ddc *hw_to_ddc(struct clk_hw *hw)
{
	return container_of(hw, struct sun4i_ddc, hw);
}

static unsigned long sun4i_ddc_calc_divider(unsigned long rate,
					    unsigned long parent_rate,
					    const u8 pre_div,
					    const u8 m_offset,
					    u8 *m, u8 *n)
{
	unsigned long best_rate = 0;
	u8 best_m = 0, best_n = 0, _m, _n;

	for (_m = 0; _m < 8; _m++) {
		for (_n = 0; _n < 8; _n++) {
			unsigned long tmp_rate;

			tmp_rate = (((parent_rate / pre_div) / 10) >> _n) /
				(_m + m_offset);

			if (tmp_rate > rate)
				continue;

			if (abs(rate - tmp_rate) < abs(rate - best_rate)) {
				best_rate = tmp_rate;
				best_m = _m;
				best_n = _n;
			}
		}
	}

	if (m && n) {
		*m = best_m;
		*n = best_n;
	}

	return best_rate;
}

static long sun4i_ddc_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	struct sun4i_ddc *ddc = hw_to_ddc(hw);

	return sun4i_ddc_calc_divider(rate, *prate, ddc->pre_div,
				      ddc->m_offset, NULL, NULL);
}

static unsigned long sun4i_ddc_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct sun4i_ddc *ddc = hw_to_ddc(hw);
	unsigned int reg;
	u8 m, n;

	regmap_field_read(ddc->reg, &reg);
	m = SUN4I_HDMI_DDC_CLK_M_GET(reg);
	n = SUN4I_HDMI_DDC_CLK_N_GET(reg);

	return (((parent_rate / ddc->pre_div) / 10) >> n) /
	       (m + ddc->m_offset);
}

static int sun4i_ddc_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct sun4i_ddc *ddc = hw_to_ddc(hw);
	u8 div_m, div_n;

	sun4i_ddc_calc_divider(rate, parent_rate, ddc->pre_div,
			       ddc->m_offset, &div_m, &div_n);

	regmap_field_write(ddc->reg,
			   SUN4I_HDMI_DDC_CLK_M(div_m) |
			   SUN4I_HDMI_DDC_CLK_N(div_n));

	return 0;
}

static const struct clk_ops sun4i_ddc_ops = {
	.recalc_rate	= sun4i_ddc_recalc_rate,
	.round_rate	= sun4i_ddc_round_rate,
	.set_rate	= sun4i_ddc_set_rate,
};

struct clk *sun4i_ddc_create(struct device *dev, struct regmap *regmap,
			     const struct sun4i_hdmi_i2c_variant *variant,
			     const struct clk *parent)
{
	struct clk_init_data init;
	struct sun4i_ddc *ddc;
	const char *parent_name;

	parent_name = __clk_get_name(parent);
	if (!parent_name)
		return ERR_PTR(-ENODEV);

	ddc = devm_kzalloc(dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return ERR_PTR(-ENOMEM);

	ddc->reg = devm_regmap_field_alloc(dev, regmap, variant->ddc_clk_reg);
	if (IS_ERR(ddc->reg))
		return ERR_CAST(ddc->reg);

	init.name = "hdmi-i2c";
	init.ops = &sun4i_ddc_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_RECALC_NEW_RATES;

	ddc->hw.init = &init;
	ddc->pre_div = variant->ddc_clk_pre_divider;
	ddc->m_offset = variant->ddc_clk_m_offset;

	return devm_clk_register(dev, &ddc->hw);
}
