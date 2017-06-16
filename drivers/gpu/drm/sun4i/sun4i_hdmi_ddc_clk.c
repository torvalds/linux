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

#include <linux/clk-provider.h>

#include "sun4i_tcon.h"
#include "sun4i_hdmi.h"

struct sun4i_ddc {
	struct clk_hw		hw;
	struct sun4i_hdmi	*hdmi;
};

static inline struct sun4i_ddc *hw_to_ddc(struct clk_hw *hw)
{
	return container_of(hw, struct sun4i_ddc, hw);
}

static unsigned long sun4i_ddc_calc_divider(unsigned long rate,
					    unsigned long parent_rate,
					    u8 *m, u8 *n)
{
	unsigned long best_rate = 0;
	u8 best_m = 0, best_n = 0, _m, _n;

	for (_m = 0; _m < 8; _m++) {
		for (_n = 0; _n < 8; _n++) {
			unsigned long tmp_rate;

			tmp_rate = (((parent_rate / 2) / 10) >> _n) / (_m + 1);

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
	return sun4i_ddc_calc_divider(rate, *prate, NULL, NULL);
}

static unsigned long sun4i_ddc_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct sun4i_ddc *ddc = hw_to_ddc(hw);
	u32 reg;
	u8 m, n;

	reg = readl(ddc->hdmi->base + SUN4I_HDMI_DDC_CLK_REG);
	m = (reg >> 3) & 0x7;
	n = reg & 0x7;

	return (((parent_rate / 2) / 10) >> n) / (m + 1);
}

static int sun4i_ddc_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct sun4i_ddc *ddc = hw_to_ddc(hw);
	u8 div_m, div_n;

	sun4i_ddc_calc_divider(rate, parent_rate, &div_m, &div_n);

	writel(SUN4I_HDMI_DDC_CLK_M(div_m) | SUN4I_HDMI_DDC_CLK_N(div_n),
	       ddc->hdmi->base + SUN4I_HDMI_DDC_CLK_REG);

	return 0;
}

static const struct clk_ops sun4i_ddc_ops = {
	.recalc_rate	= sun4i_ddc_recalc_rate,
	.round_rate	= sun4i_ddc_round_rate,
	.set_rate	= sun4i_ddc_set_rate,
};

int sun4i_ddc_create(struct sun4i_hdmi *hdmi, struct clk *parent)
{
	struct clk_init_data init;
	struct sun4i_ddc *ddc;
	const char *parent_name;

	parent_name = __clk_get_name(parent);
	if (!parent_name)
		return -ENODEV;

	ddc = devm_kzalloc(hdmi->dev, sizeof(*ddc), GFP_KERNEL);
	if (!ddc)
		return -ENOMEM;

	init.name = "hdmi-ddc";
	init.ops = &sun4i_ddc_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	ddc->hdmi = hdmi;
	ddc->hw.init = &init;

	hdmi->ddc_clk = devm_clk_register(hdmi->dev, &ddc->hw);
	if (IS_ERR(hdmi->ddc_clk))
		return PTR_ERR(hdmi->ddc_clk);

	return 0;
}
