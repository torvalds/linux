// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 PLL Clock Generator Driver
 *
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 * Copyright (C) 2023 Emil Renner Berthing <emil.renner.berthing@canonical.com>
 *
 * This driver is about to register JH7110 PLL clock generator and support ops.
 * The JH7110 have three PLL clock, PLL0, PLL1 and PLL2.
 * Each PLL clocks work in integer mode or fraction mode by some dividers,
 * and the configuration registers and dividers are set in several syscon registers.
 * The formula for calculating frequency is:
 * Fvco = Fref * (NI + NF) / M / Q1
 * Fref: OSC source clock rate
 * NI: integer frequency dividing ratio of feedback divider, set by fbdiv[11:0].
 * NF: fractional frequency dividing ratio, set by frac[23:0]. NF = frac[23:0] / 2^24 = 0 ~ 0.999.
 * M: frequency dividing ratio of pre-divider, set by prediv[5:0].
 * Q1: frequency dividing ratio of post divider, set by 2^postdiv1[1:0], eg. 1, 2, 4 or 8.
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/starfive,jh7110-crg.h>

/* this driver expects a 24MHz input frequency from the oscillator */
#define JH7110_PLL_OSC_RATE		24000000UL

#define JH7110_PLL0_PD_OFFSET		0x18
#define JH7110_PLL0_DACPD_SHIFT		24
#define JH7110_PLL0_DACPD_MASK		BIT(24)
#define JH7110_PLL0_DSMPD_SHIFT		25
#define JH7110_PLL0_DSMPD_MASK		BIT(25)
#define JH7110_PLL0_FBDIV_OFFSET	0x1c
#define JH7110_PLL0_FBDIV_SHIFT		0
#define JH7110_PLL0_FBDIV_MASK		GENMASK(11, 0)
#define JH7110_PLL0_FRAC_OFFSET		0x20
#define JH7110_PLL0_PREDIV_OFFSET	0x24

#define JH7110_PLL1_PD_OFFSET		0x24
#define JH7110_PLL1_DACPD_SHIFT		15
#define JH7110_PLL1_DACPD_MASK		BIT(15)
#define JH7110_PLL1_DSMPD_SHIFT		16
#define JH7110_PLL1_DSMPD_MASK		BIT(16)
#define JH7110_PLL1_FBDIV_OFFSET	0x24
#define JH7110_PLL1_FBDIV_SHIFT		17
#define JH7110_PLL1_FBDIV_MASK		GENMASK(28, 17)
#define JH7110_PLL1_FRAC_OFFSET		0x28
#define JH7110_PLL1_PREDIV_OFFSET	0x2c

#define JH7110_PLL2_PD_OFFSET		0x2c
#define JH7110_PLL2_DACPD_SHIFT		15
#define JH7110_PLL2_DACPD_MASK		BIT(15)
#define JH7110_PLL2_DSMPD_SHIFT		16
#define JH7110_PLL2_DSMPD_MASK		BIT(16)
#define JH7110_PLL2_FBDIV_OFFSET	0x2c
#define JH7110_PLL2_FBDIV_SHIFT		17
#define JH7110_PLL2_FBDIV_MASK		GENMASK(28, 17)
#define JH7110_PLL2_FRAC_OFFSET		0x30
#define JH7110_PLL2_PREDIV_OFFSET	0x34

#define JH7110_PLL_FRAC_SHIFT		0
#define JH7110_PLL_FRAC_MASK		GENMASK(23, 0)
#define JH7110_PLL_POSTDIV1_SHIFT	28
#define JH7110_PLL_POSTDIV1_MASK	GENMASK(29, 28)
#define JH7110_PLL_PREDIV_SHIFT		0
#define JH7110_PLL_PREDIV_MASK		GENMASK(5, 0)

enum jh7110_pll_mode {
	JH7110_PLL_MODE_FRACTION,
	JH7110_PLL_MODE_INTEGER,
};

struct jh7110_pll_preset {
	unsigned long freq;
	u32 frac;		/* frac value should be decimals multiplied by 2^24 */
	unsigned fbdiv    : 12;	/* fbdiv value should be 8 to 4095 */
	unsigned prediv   :  6;
	unsigned postdiv1 :  2;
	unsigned mode     :  1;
};

struct jh7110_pll_info {
	char *name;
	const struct jh7110_pll_preset *presets;
	unsigned int npresets;
	struct {
		unsigned int pd;
		unsigned int fbdiv;
		unsigned int frac;
		unsigned int prediv;
	} offsets;
	struct {
		u32 dacpd;
		u32 dsmpd;
		u32 fbdiv;
	} masks;
	struct {
		char dacpd;
		char dsmpd;
		char fbdiv;
	} shifts;
};

#define _JH7110_PLL(_idx, _name, _presets)				\
	[_idx] = {							\
		.name = _name,						\
		.presets = _presets,					\
		.npresets = ARRAY_SIZE(_presets),			\
		.offsets = {						\
			.pd = JH7110_PLL##_idx##_PD_OFFSET,		\
			.fbdiv = JH7110_PLL##_idx##_FBDIV_OFFSET,	\
			.frac = JH7110_PLL##_idx##_FRAC_OFFSET,		\
			.prediv = JH7110_PLL##_idx##_PREDIV_OFFSET,	\
		},							\
		.masks = {						\
			.dacpd = JH7110_PLL##_idx##_DACPD_MASK,		\
			.dsmpd = JH7110_PLL##_idx##_DSMPD_MASK,		\
			.fbdiv = JH7110_PLL##_idx##_FBDIV_MASK,		\
		},							\
		.shifts = {						\
			.dacpd = JH7110_PLL##_idx##_DACPD_SHIFT,	\
			.dsmpd = JH7110_PLL##_idx##_DSMPD_SHIFT,	\
			.fbdiv = JH7110_PLL##_idx##_FBDIV_SHIFT,	\
		},							\
	}
#define JH7110_PLL(idx, name, presets) _JH7110_PLL(idx, name, presets)

struct jh7110_pll_data {
	struct clk_hw hw;
	unsigned int idx;
};

struct jh7110_pll_priv {
	struct device *dev;
	struct regmap *regmap;
	struct jh7110_pll_data pll[JH7110_PLLCLK_END];
};

struct jh7110_pll_regvals {
	u32 dacpd;
	u32 dsmpd;
	u32 fbdiv;
	u32 frac;
	u32 postdiv1;
	u32 prediv;
};

/*
 * Because the pll frequency is relatively fixed,
 * it cannot be set arbitrarily, so it needs a specific configuration.
 * PLL0 frequency should be multiple of 125MHz (USB frequency).
 */
static const struct jh7110_pll_preset jh7110_pll0_presets[] = {
	{
		.freq = 375000000,
		.fbdiv = 125,
		.prediv = 8,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 500000000,
		.fbdiv = 125,
		.prediv = 6,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 625000000,
		.fbdiv = 625,
		.prediv = 24,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 750000000,
		.fbdiv = 125,
		.prediv = 4,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 875000000,
		.fbdiv = 875,
		.prediv = 24,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1000000000,
		.fbdiv = 125,
		.prediv = 3,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1250000000,
		.fbdiv = 625,
		.prediv = 12,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1375000000,
		.fbdiv = 1375,
		.prediv = 24,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1500000000,
		.fbdiv = 125,
		.prediv = 2,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	},
};

static const struct jh7110_pll_preset jh7110_pll1_presets[] = {
	{
		.freq = 1066000000,
		.fbdiv = 533,
		.prediv = 12,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1200000000,
		.fbdiv = 50,
		.prediv = 1,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1400000000,
		.fbdiv = 350,
		.prediv = 6,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1600000000,
		.fbdiv = 200,
		.prediv = 3,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	},
};

static const struct jh7110_pll_preset jh7110_pll2_presets[] = {
	{
		.freq = 1188000000,
		.fbdiv = 99,
		.prediv = 2,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	}, {
		.freq = 1228800000,
		.fbdiv = 256,
		.prediv = 5,
		.postdiv1 = 0,
		.mode = JH7110_PLL_MODE_INTEGER,
	},
};

static const struct jh7110_pll_info jh7110_plls[JH7110_PLLCLK_END] = {
	JH7110_PLL(JH7110_PLLCLK_PLL0_OUT, "pll0_out", jh7110_pll0_presets),
	JH7110_PLL(JH7110_PLLCLK_PLL1_OUT, "pll1_out", jh7110_pll1_presets),
	JH7110_PLL(JH7110_PLLCLK_PLL2_OUT, "pll2_out", jh7110_pll2_presets),
};

static struct jh7110_pll_data *jh7110_pll_data_from(struct clk_hw *hw)
{
	return container_of(hw, struct jh7110_pll_data, hw);
}

static struct jh7110_pll_priv *jh7110_pll_priv_from(struct jh7110_pll_data *pll)
{
	return container_of(pll, struct jh7110_pll_priv, pll[pll->idx]);
}

static void jh7110_pll_regvals_get(struct regmap *regmap,
				   const struct jh7110_pll_info *info,
				   struct jh7110_pll_regvals *ret)
{
	u32 val;

	regmap_read(regmap, info->offsets.pd, &val);
	ret->dacpd = (val & info->masks.dacpd) >> info->shifts.dacpd;
	ret->dsmpd = (val & info->masks.dsmpd) >> info->shifts.dsmpd;

	regmap_read(regmap, info->offsets.fbdiv, &val);
	ret->fbdiv = (val & info->masks.fbdiv) >> info->shifts.fbdiv;

	regmap_read(regmap, info->offsets.frac, &val);
	ret->frac = (val & JH7110_PLL_FRAC_MASK) >> JH7110_PLL_FRAC_SHIFT;
	ret->postdiv1 = (val & JH7110_PLL_POSTDIV1_MASK) >> JH7110_PLL_POSTDIV1_SHIFT;

	regmap_read(regmap, info->offsets.prediv, &val);
	ret->prediv = (val & JH7110_PLL_PREDIV_MASK) >> JH7110_PLL_PREDIV_SHIFT;
}

static unsigned long jh7110_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct jh7110_pll_data *pll = jh7110_pll_data_from(hw);
	struct jh7110_pll_priv *priv = jh7110_pll_priv_from(pll);
	struct jh7110_pll_regvals val;
	unsigned long rate;

	jh7110_pll_regvals_get(priv->regmap, &jh7110_plls[pll->idx], &val);

	/*
	 * dacpd = dsmpd = 0: fraction mode
	 * dacpd = dsmpd = 1: integer mode, frac value ignored
	 *
	 * rate = parent * (fbdiv + frac/2^24) / prediv / 2^postdiv1
	 *      = (parent * fbdiv + parent * frac / 2^24) / (prediv * 2^postdiv1)
	 */
	if (val.dacpd == 0 && val.dsmpd == 0)
		rate = parent_rate * val.frac / (1UL << 24);
	else if (val.dacpd == 1 && val.dsmpd == 1)
		rate = 0;
	else
		return 0;

	rate += parent_rate * val.fbdiv;
	rate /= val.prediv << val.postdiv1;

	return rate;
}

static int jh7110_pll_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct jh7110_pll_data *pll = jh7110_pll_data_from(hw);
	const struct jh7110_pll_info *info = &jh7110_plls[pll->idx];
	const struct jh7110_pll_preset *selected = &info->presets[0];
	unsigned int idx;

	/* if the parent rate doesn't match our expectations the presets won't work */
	if (req->best_parent_rate != JH7110_PLL_OSC_RATE) {
		req->rate = jh7110_pll_recalc_rate(hw, req->best_parent_rate);
		return 0;
	}

	/* find highest rate lower or equal to the requested rate */
	for (idx = 1; idx < info->npresets; idx++) {
		const struct jh7110_pll_preset *val = &info->presets[idx];

		if (req->rate < val->freq)
			break;

		selected = val;
	}

	req->rate = selected->freq;
	return 0;
}

static int jh7110_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct jh7110_pll_data *pll = jh7110_pll_data_from(hw);
	struct jh7110_pll_priv *priv = jh7110_pll_priv_from(pll);
	const struct jh7110_pll_info *info = &jh7110_plls[pll->idx];
	const struct jh7110_pll_preset *val;
	unsigned int idx;

	/* if the parent rate doesn't match our expectations the presets won't work */
	if (parent_rate != JH7110_PLL_OSC_RATE)
		return -EINVAL;

	for (idx = 0, val = &info->presets[0]; idx < info->npresets; idx++, val++) {
		if (val->freq == rate)
			goto found;
	}
	return -EINVAL;

found:
	if (val->mode == JH7110_PLL_MODE_FRACTION)
		regmap_update_bits(priv->regmap, info->offsets.frac, JH7110_PLL_FRAC_MASK,
				   val->frac << JH7110_PLL_FRAC_SHIFT);

	regmap_update_bits(priv->regmap, info->offsets.pd, info->masks.dacpd,
			   (u32)val->mode << info->shifts.dacpd);
	regmap_update_bits(priv->regmap, info->offsets.pd, info->masks.dsmpd,
			   (u32)val->mode << info->shifts.dsmpd);
	regmap_update_bits(priv->regmap, info->offsets.prediv, JH7110_PLL_PREDIV_MASK,
			   (u32)val->prediv << JH7110_PLL_PREDIV_SHIFT);
	regmap_update_bits(priv->regmap, info->offsets.fbdiv, info->masks.fbdiv,
			   val->fbdiv << info->shifts.fbdiv);
	regmap_update_bits(priv->regmap, info->offsets.frac, JH7110_PLL_POSTDIV1_MASK,
			   (u32)val->postdiv1 << JH7110_PLL_POSTDIV1_SHIFT);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int jh7110_pll_registers_read(struct seq_file *s, void *unused)
{
	struct jh7110_pll_data *pll = s->private;
	struct jh7110_pll_priv *priv = jh7110_pll_priv_from(pll);
	struct jh7110_pll_regvals val;

	jh7110_pll_regvals_get(priv->regmap, &jh7110_plls[pll->idx], &val);

	seq_printf(s, "fbdiv=%u\n"
		      "frac=%u\n"
		      "prediv=%u\n"
		      "postdiv1=%u\n"
		      "dacpd=%u\n"
		      "dsmpd=%u\n",
		      val.fbdiv, val.frac, val.prediv, val.postdiv1,
		      val.dacpd, val.dsmpd);

	return 0;
}

static int jh7110_pll_registers_open(struct inode *inode, struct file *f)
{
	return single_open(f, jh7110_pll_registers_read, inode->i_private);
}

static const struct file_operations jh7110_pll_registers_ops = {
	.owner = THIS_MODULE,
	.open = jh7110_pll_registers_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

static void jh7110_pll_debug_init(struct clk_hw *hw, struct dentry *dentry)
{
	struct jh7110_pll_data *pll = jh7110_pll_data_from(hw);

	debugfs_create_file("registers", 0400, dentry, pll,
			    &jh7110_pll_registers_ops);
}
#else
#define jh7110_pll_debug_init NULL
#endif

static const struct clk_ops jh7110_pll_ops = {
	.recalc_rate = jh7110_pll_recalc_rate,
	.determine_rate = jh7110_pll_determine_rate,
	.set_rate = jh7110_pll_set_rate,
	.debug_init = jh7110_pll_debug_init,
};

static struct clk_hw *jh7110_pll_get(struct of_phandle_args *clkspec, void *data)
{
	struct jh7110_pll_priv *priv = data;
	unsigned int idx = clkspec->args[0];

	if (idx < JH7110_PLLCLK_END)
		return &priv->pll[idx].hw;

	return ERR_PTR(-EINVAL);
}

static int __init jh7110_pll_probe(struct platform_device *pdev)
{
	struct jh7110_pll_priv *priv;
	unsigned int idx;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->regmap = syscon_node_to_regmap(priv->dev->of_node->parent);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	for (idx = 0; idx < JH7110_PLLCLK_END; idx++) {
		struct clk_parent_data parents = {
			.index = 0,
		};
		struct clk_init_data init = {
			.name = jh7110_plls[idx].name,
			.ops = &jh7110_pll_ops,
			.parent_data = &parents,
			.num_parents = 1,
			.flags = 0,
		};
		struct jh7110_pll_data *pll = &priv->pll[idx];

		pll->hw.init = &init;
		pll->idx = idx;

		ret = devm_clk_hw_register(&pdev->dev, &pll->hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(&pdev->dev, jh7110_pll_get, priv);
}

static const struct of_device_id jh7110_pll_match[] = {
	{ .compatible = "starfive,jh7110-pll" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jh7110_pll_match);

static struct platform_driver jh7110_pll_driver = {
	.driver = {
		.name = "clk-starfive-jh7110-pll",
		.of_match_table = jh7110_pll_match,
	},
};
builtin_platform_driver_probe(jh7110_pll_driver, jh7110_pll_probe);
