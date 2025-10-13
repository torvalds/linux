// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Microchip Sparx5 SoC Clock driver.
 *
 * Copyright (c) 2019 Microchip Inc.
 *
 * Author: Lars Povlsen <lars.povlsen@microchip.com>
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/bitfield.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/microchip,sparx5.h>

#define PLL_DIV		GENMASK(7, 0)
#define PLL_PRE_DIV	GENMASK(10, 8)
#define PLL_ROT_DIR	BIT(11)
#define PLL_ROT_SEL	GENMASK(13, 12)
#define PLL_ROT_ENA	BIT(14)
#define PLL_CLK_ENA	BIT(15)

#define MAX_SEL 4
#define MAX_PRE BIT(3)

static const u8 sel_rates[MAX_SEL] = { 0, 2*8, 2*4, 2*2 };

static const char *clk_names[N_CLOCKS] = {
	"core", "ddr", "cpu2", "arm2",
	"aux1", "aux2", "aux3", "aux4",
	"synce",
};

struct s5_hw_clk {
	struct clk_hw hw;
	void __iomem *reg;
};

struct s5_clk_data {
	void __iomem *base;
	struct s5_hw_clk s5_hw[N_CLOCKS];
};

struct s5_pll_conf {
	unsigned long freq;
	u8 div;
	bool rot_ena;
	u8 rot_sel;
	u8 rot_dir;
	u8 pre_div;
};

#define to_s5_pll(hw) container_of(hw, struct s5_hw_clk, hw)

static unsigned long s5_calc_freq(unsigned long parent_rate,
				  const struct s5_pll_conf *conf)
{
	unsigned long rate = parent_rate / conf->div;

	if (conf->rot_ena) {
		int sign = conf->rot_dir ? -1 : 1;
		int divt = sel_rates[conf->rot_sel] * (1 + conf->pre_div);
		int divb = divt + sign;

		rate = mult_frac(rate, divt, divb);
		rate = roundup(rate, 1000);
	}

	return rate;
}

static void s5_search_fractional(unsigned long rate,
				 unsigned long parent_rate,
				 int div,
				 struct s5_pll_conf *conf)
{
	struct s5_pll_conf best;
	ulong cur_offset, best_offset = rate;
	int d, i, j;

	memset(conf, 0, sizeof(*conf));
	conf->div = div;
	conf->rot_ena = 1;	/* Fractional rate */

	for (d = 0; best_offset > 0 && d <= 1 ; d++) {
		conf->rot_dir = !!d;
		for (i = 0; best_offset > 0 && i < MAX_PRE; i++) {
			conf->pre_div = i;
			for (j = 1; best_offset > 0 && j < MAX_SEL; j++) {
				conf->rot_sel = j;
				conf->freq = s5_calc_freq(parent_rate, conf);
				cur_offset = abs(rate - conf->freq);
				if (cur_offset < best_offset) {
					best_offset = cur_offset;
					best = *conf;
				}
			}
		}
	}

	/* Best match */
	*conf = best;
}

static unsigned long s5_calc_params(unsigned long rate,
				    unsigned long parent_rate,
				    struct s5_pll_conf *conf)
{
	if (parent_rate % rate) {
		struct s5_pll_conf alt1, alt2;
		int div;

		div = DIV_ROUND_CLOSEST_ULL(parent_rate, rate);
		s5_search_fractional(rate, parent_rate, div, &alt1);

		/* Straight match? */
		if (alt1.freq == rate) {
			*conf = alt1;
		} else {
			/* Try without rounding divider */
			div = parent_rate / rate;
			if (div != alt1.div) {
				s5_search_fractional(rate, parent_rate, div,
						     &alt2);
				/* Select the better match */
				if (abs(rate - alt1.freq) <
				    abs(rate - alt2.freq))
					*conf = alt1;
				else
					*conf = alt2;
			}
		}
	} else {
		/* Straight fit */
		memset(conf, 0, sizeof(*conf));
		conf->div = parent_rate / rate;
	}

	return conf->freq;
}

static int s5_pll_enable(struct clk_hw *hw)
{
	struct s5_hw_clk *pll = to_s5_pll(hw);
	u32 val = readl(pll->reg);

	val |= PLL_CLK_ENA;
	writel(val, pll->reg);

	return 0;
}

static void s5_pll_disable(struct clk_hw *hw)
{
	struct s5_hw_clk *pll = to_s5_pll(hw);
	u32 val = readl(pll->reg);

	val &= ~PLL_CLK_ENA;
	writel(val, pll->reg);
}

static int s5_pll_set_rate(struct clk_hw *hw,
			   unsigned long rate,
			   unsigned long parent_rate)
{
	struct s5_hw_clk *pll = to_s5_pll(hw);
	struct s5_pll_conf conf;
	unsigned long eff_rate;
	u32 val;

	eff_rate = s5_calc_params(rate, parent_rate, &conf);
	if (eff_rate != rate)
		return -EOPNOTSUPP;

	val = readl(pll->reg) & PLL_CLK_ENA;
	val |= FIELD_PREP(PLL_DIV, conf.div);
	if (conf.rot_ena) {
		val |= PLL_ROT_ENA;
		val |= FIELD_PREP(PLL_ROT_SEL, conf.rot_sel);
		val |= FIELD_PREP(PLL_PRE_DIV, conf.pre_div);
		if (conf.rot_dir)
			val |= PLL_ROT_DIR;
	}
	writel(val, pll->reg);

	return 0;
}

static unsigned long s5_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct s5_hw_clk *pll = to_s5_pll(hw);
	struct s5_pll_conf conf;
	u32 val;

	val = readl(pll->reg);

	if (val & PLL_CLK_ENA) {
		conf.div     = FIELD_GET(PLL_DIV, val);
		conf.pre_div = FIELD_GET(PLL_PRE_DIV, val);
		conf.rot_ena = FIELD_GET(PLL_ROT_ENA, val);
		conf.rot_dir = FIELD_GET(PLL_ROT_DIR, val);
		conf.rot_sel = FIELD_GET(PLL_ROT_SEL, val);

		conf.freq = s5_calc_freq(parent_rate, &conf);
	} else {
		conf.freq = 0;
	}

	return conf.freq;
}

static int s5_pll_determine_rate(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	struct s5_pll_conf conf;

	req->rate = s5_calc_params(req->rate, req->best_parent_rate, &conf);

	return 0;
}

static const struct clk_ops s5_pll_ops = {
	.enable		= s5_pll_enable,
	.disable	= s5_pll_disable,
	.set_rate	= s5_pll_set_rate,
	.determine_rate = s5_pll_determine_rate,
	.recalc_rate	= s5_pll_recalc_rate,
};

static struct clk_hw *s5_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct s5_clk_data *s5_clk = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= N_CLOCKS) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &s5_clk->s5_hw[idx].hw;
}

static int s5_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int i, ret;
	struct s5_clk_data *s5_clk;
	struct clk_parent_data pdata = { .index = 0 };
	struct clk_init_data init = {
		.ops = &s5_pll_ops,
		.num_parents = 1,
		.parent_data = &pdata,
	};

	s5_clk = devm_kzalloc(dev, sizeof(*s5_clk), GFP_KERNEL);
	if (!s5_clk)
		return -ENOMEM;

	s5_clk->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(s5_clk->base))
		return PTR_ERR(s5_clk->base);

	for (i = 0; i < N_CLOCKS; i++) {
		struct s5_hw_clk *s5_hw = &s5_clk->s5_hw[i];

		init.name = clk_names[i];
		s5_hw->reg = s5_clk->base + (i * 4);
		s5_hw->hw.init = &init;
		ret = devm_clk_hw_register(dev, &s5_hw->hw);
		if (ret) {
			dev_err(dev, "failed to register %s clock\n",
				init.name);
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, s5_clk_hw_get, s5_clk);
}

static const struct of_device_id s5_clk_dt_ids[] = {
	{ .compatible = "microchip,sparx5-dpll", },
	{ }
};
MODULE_DEVICE_TABLE(of, s5_clk_dt_ids);

static struct platform_driver s5_clk_driver = {
	.probe  = s5_clk_probe,
	.driver = {
		.name = "sparx5-clk",
		.of_match_table = s5_clk_dt_ids,
	},
};
builtin_platform_driver(s5_clk_driver);
