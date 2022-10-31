// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017-2018 NXP.
 */

#define pr_fmt(fmt) "pll14xx: " fmt

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include "clk.h"

#define GNRL_CTL	0x0
#define DIV_CTL0	0x4
#define DIV_CTL1	0x8
#define LOCK_STATUS	BIT(31)
#define LOCK_SEL_MASK	BIT(29)
#define CLKE_MASK	BIT(11)
#define RST_MASK	BIT(9)
#define BYPASS_MASK	BIT(4)
#define MDIV_MASK	GENMASK(21, 12)
#define PDIV_MASK	GENMASK(9, 4)
#define SDIV_MASK	GENMASK(2, 0)
#define KDIV_MASK	GENMASK(15, 0)
#define KDIV_MIN	SHRT_MIN
#define KDIV_MAX	SHRT_MAX

#define LOCK_TIMEOUT_US		10000

struct clk_pll14xx {
	struct clk_hw			hw;
	void __iomem			*base;
	enum imx_pll14xx_type		type;
	const struct imx_pll14xx_rate_table *rate_table;
	int rate_count;
};

#define to_clk_pll14xx(_hw) container_of(_hw, struct clk_pll14xx, hw)

static const struct imx_pll14xx_rate_table imx_pll1416x_tbl[] = {
	PLL_1416X_RATE(1800000000U, 225, 3, 0),
	PLL_1416X_RATE(1600000000U, 200, 3, 0),
	PLL_1416X_RATE(1500000000U, 375, 3, 1),
	PLL_1416X_RATE(1400000000U, 350, 3, 1),
	PLL_1416X_RATE(1200000000U, 300, 3, 1),
	PLL_1416X_RATE(1000000000U, 250, 3, 1),
	PLL_1416X_RATE(800000000U,  200, 3, 1),
	PLL_1416X_RATE(750000000U,  250, 2, 2),
	PLL_1416X_RATE(700000000U,  350, 3, 2),
	PLL_1416X_RATE(640000000U,  320, 3, 2),
	PLL_1416X_RATE(600000000U,  300, 3, 2),
	PLL_1416X_RATE(320000000U,  160, 3, 2),
};

static const struct imx_pll14xx_rate_table imx_pll1443x_tbl[] = {
	PLL_1443X_RATE(1039500000U, 173, 2, 1, 16384),
	PLL_1443X_RATE(650000000U, 325, 3, 2, 0),
	PLL_1443X_RATE(594000000U, 198, 2, 2, 0),
	PLL_1443X_RATE(519750000U, 173, 2, 2, 16384),
	PLL_1443X_RATE(393216000U, 262, 2, 3, 9437),
	PLL_1443X_RATE(361267200U, 361, 3, 3, 17511),
};

struct imx_pll14xx_clk imx_1443x_pll = {
	.type = PLL_1443X,
	.rate_table = imx_pll1443x_tbl,
	.rate_count = ARRAY_SIZE(imx_pll1443x_tbl),
};
EXPORT_SYMBOL_GPL(imx_1443x_pll);

struct imx_pll14xx_clk imx_1443x_dram_pll = {
	.type = PLL_1443X,
	.rate_table = imx_pll1443x_tbl,
	.rate_count = ARRAY_SIZE(imx_pll1443x_tbl),
	.flags = CLK_GET_RATE_NOCACHE,
};
EXPORT_SYMBOL_GPL(imx_1443x_dram_pll);

struct imx_pll14xx_clk imx_1416x_pll = {
	.type = PLL_1416X,
	.rate_table = imx_pll1416x_tbl,
	.rate_count = ARRAY_SIZE(imx_pll1416x_tbl),
};
EXPORT_SYMBOL_GPL(imx_1416x_pll);

static const struct imx_pll14xx_rate_table *imx_get_pll_settings(
		struct clk_pll14xx *pll, unsigned long rate)
{
	const struct imx_pll14xx_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++)
		if (rate == rate_table[i].rate)
			return &rate_table[i];

	return NULL;
}

static long pll14xx_calc_rate(struct clk_pll14xx *pll, int mdiv, int pdiv,
			      int sdiv, int kdiv, unsigned long prate)
{
	u64 fvco = prate;

	/* fvco = (m * 65536 + k) * Fin / (p * 65536) */
	fvco *= (mdiv * 65536 + kdiv);
	pdiv *= 65536;

	do_div(fvco, pdiv << sdiv);

	return fvco;
}

static long pll1443x_calc_kdiv(int mdiv, int pdiv, int sdiv,
		unsigned long rate, unsigned long prate)
{
	long kdiv;

	/* calc kdiv = round(rate * pdiv * 65536 * 2^sdiv / prate) - (mdiv * 65536) */
	kdiv = ((rate * ((pdiv * 65536) << sdiv) + prate / 2) / prate) - (mdiv * 65536);

	return clamp_t(short, kdiv, KDIV_MIN, KDIV_MAX);
}

static void imx_pll14xx_calc_settings(struct clk_pll14xx *pll, unsigned long rate,
				      unsigned long prate, struct imx_pll14xx_rate_table *t)
{
	u32 pll_div_ctl0, pll_div_ctl1;
	int mdiv, pdiv, sdiv, kdiv;
	long fvco, rate_min, rate_max, dist, best = LONG_MAX;
	const struct imx_pll14xx_rate_table *tt;

	/*
	 * Fractional PLL constrains:
	 *
	 * a) 6MHz <= prate <= 25MHz
	 * b) 1 <= p <= 63 (1 <= p <= 4 prate = 24MHz)
	 * c) 64 <= m <= 1023
	 * d) 0 <= s <= 6
	 * e) -32768 <= k <= 32767
	 *
	 * fvco = (m * 65536 + k) * prate / (p * 65536)
	 */

	/* First try if we can get the desired rate from one of the static entries */
	tt = imx_get_pll_settings(pll, rate);
	if (tt) {
		pr_debug("%s: in=%ld, want=%ld, Using PLL setting from table\n",
			 clk_hw_get_name(&pll->hw), prate, rate);
		t->rate = tt->rate;
		t->mdiv = tt->mdiv;
		t->pdiv = tt->pdiv;
		t->sdiv = tt->sdiv;
		t->kdiv = tt->kdiv;
		return;
	}

	pll_div_ctl0 = readl_relaxed(pll->base + DIV_CTL0);
	mdiv = FIELD_GET(MDIV_MASK, pll_div_ctl0);
	pdiv = FIELD_GET(PDIV_MASK, pll_div_ctl0);
	sdiv = FIELD_GET(SDIV_MASK, pll_div_ctl0);
	pll_div_ctl1 = readl_relaxed(pll->base + DIV_CTL1);

	/* Then see if we can get the desired rate by only adjusting kdiv (glitch free) */
	rate_min = pll14xx_calc_rate(pll, mdiv, pdiv, sdiv, KDIV_MIN, prate);
	rate_max = pll14xx_calc_rate(pll, mdiv, pdiv, sdiv, KDIV_MAX, prate);

	if (rate >= rate_min && rate <= rate_max) {
		kdiv = pll1443x_calc_kdiv(mdiv, pdiv, sdiv, rate, prate);
		pr_debug("%s: in=%ld, want=%ld Only adjust kdiv %ld -> %d\n",
			 clk_hw_get_name(&pll->hw), prate, rate,
			 FIELD_GET(KDIV_MASK, pll_div_ctl1), kdiv);
		fvco = pll14xx_calc_rate(pll, mdiv, pdiv, sdiv, kdiv, prate);
		t->rate = (unsigned int)fvco;
		t->mdiv = mdiv;
		t->pdiv = pdiv;
		t->sdiv = sdiv;
		t->kdiv = kdiv;
		return;
	}

	/* Finally calculate best values */
	for (pdiv = 1; pdiv <= 7; pdiv++) {
		for (sdiv = 0; sdiv <= 6; sdiv++) {
			/* calc mdiv = round(rate * pdiv * 2^sdiv) / prate) */
			mdiv = DIV_ROUND_CLOSEST(rate * (pdiv << sdiv), prate);
			mdiv = clamp(mdiv, 64, 1023);

			kdiv = pll1443x_calc_kdiv(mdiv, pdiv, sdiv, rate, prate);
			fvco = pll14xx_calc_rate(pll, mdiv, pdiv, sdiv, kdiv, prate);

			/* best match */
			dist = abs((long)rate - (long)fvco);
			if (dist < best) {
				best = dist;
				t->rate = (unsigned int)fvco;
				t->mdiv = mdiv;
				t->pdiv = pdiv;
				t->sdiv = sdiv;
				t->kdiv = kdiv;

				if (!dist)
					goto found;
			}
		}
	}
found:
	pr_debug("%s: in=%ld, want=%ld got=%d (pdiv=%d sdiv=%d mdiv=%d kdiv=%d)\n",
		 clk_hw_get_name(&pll->hw), prate, rate, t->rate, t->pdiv, t->sdiv,
		 t->mdiv, t->kdiv);
}

static long clk_pll1416x_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	const struct imx_pll14xx_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assuming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++)
		if (rate >= rate_table[i].rate)
			return rate_table[i].rate;

	/* return minimum supported value */
	return rate_table[pll->rate_count - 1].rate;
}

static long clk_pll1443x_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	struct imx_pll14xx_rate_table t;

	imx_pll14xx_calc_settings(pll, rate, *prate, &t);

	return t.rate;
}

static unsigned long clk_pll14xx_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 mdiv, pdiv, sdiv, kdiv, pll_div_ctl0, pll_div_ctl1;

	pll_div_ctl0 = readl_relaxed(pll->base + DIV_CTL0);
	mdiv = FIELD_GET(MDIV_MASK, pll_div_ctl0);
	pdiv = FIELD_GET(PDIV_MASK, pll_div_ctl0);
	sdiv = FIELD_GET(SDIV_MASK, pll_div_ctl0);

	if (pll->type == PLL_1443X) {
		pll_div_ctl1 = readl_relaxed(pll->base + DIV_CTL1);
		kdiv = FIELD_GET(KDIV_MASK, pll_div_ctl1);
	} else {
		kdiv = 0;
	}

	return pll14xx_calc_rate(pll, mdiv, pdiv, sdiv, kdiv, parent_rate);
}

static inline bool clk_pll14xx_mp_change(const struct imx_pll14xx_rate_table *rate,
					  u32 pll_div)
{
	u32 old_mdiv, old_pdiv;

	old_mdiv = FIELD_GET(MDIV_MASK, pll_div);
	old_pdiv = FIELD_GET(PDIV_MASK, pll_div);

	return rate->mdiv != old_mdiv || rate->pdiv != old_pdiv;
}

static int clk_pll14xx_wait_lock(struct clk_pll14xx *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base + GNRL_CTL, val, val & LOCK_STATUS, 0,
			LOCK_TIMEOUT_US);
}

static int clk_pll1416x_set_rate(struct clk_hw *hw, unsigned long drate,
				 unsigned long prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	const struct imx_pll14xx_rate_table *rate;
	u32 tmp, div_val;
	int ret;

	rate = imx_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("Invalid rate %lu for pll clk %s\n", drate,
		       clk_hw_get_name(hw));
		return -EINVAL;
	}

	tmp = readl_relaxed(pll->base + DIV_CTL0);

	if (!clk_pll14xx_mp_change(rate, tmp)) {
		tmp &= ~SDIV_MASK;
		tmp |= FIELD_PREP(SDIV_MASK, rate->sdiv);
		writel_relaxed(tmp, pll->base + DIV_CTL0);

		return 0;
	}

	/* Bypass clock and set lock to pll output lock */
	tmp = readl_relaxed(pll->base + GNRL_CTL);
	tmp |= LOCK_SEL_MASK;
	writel_relaxed(tmp, pll->base + GNRL_CTL);

	/* Enable RST */
	tmp &= ~RST_MASK;
	writel_relaxed(tmp, pll->base + GNRL_CTL);

	/* Enable BYPASS */
	tmp |= BYPASS_MASK;
	writel(tmp, pll->base + GNRL_CTL);

	div_val = FIELD_PREP(MDIV_MASK, rate->mdiv) | FIELD_PREP(PDIV_MASK, rate->pdiv) |
		FIELD_PREP(SDIV_MASK, rate->sdiv);
	writel_relaxed(div_val, pll->base + DIV_CTL0);

	/*
	 * According to SPEC, t3 - t2 need to be greater than
	 * 1us and 1/FREF, respectively.
	 * FREF is FIN / Prediv, the prediv is [1, 63], so choose
	 * 3us.
	 */
	udelay(3);

	/* Disable RST */
	tmp |= RST_MASK;
	writel_relaxed(tmp, pll->base + GNRL_CTL);

	/* Wait Lock */
	ret = clk_pll14xx_wait_lock(pll);
	if (ret)
		return ret;

	/* Bypass */
	tmp &= ~BYPASS_MASK;
	writel_relaxed(tmp, pll->base + GNRL_CTL);

	return 0;
}

static int clk_pll1443x_set_rate(struct clk_hw *hw, unsigned long drate,
				 unsigned long prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	struct imx_pll14xx_rate_table rate;
	u32 gnrl_ctl, div_ctl0;
	int ret;

	imx_pll14xx_calc_settings(pll, drate, prate, &rate);

	div_ctl0 = readl_relaxed(pll->base + DIV_CTL0);

	if (!clk_pll14xx_mp_change(&rate, div_ctl0)) {
		/* only sdiv and/or kdiv changed - no need to RESET PLL */
		div_ctl0 &= ~SDIV_MASK;
		div_ctl0 |= FIELD_PREP(SDIV_MASK, rate.sdiv);
		writel_relaxed(div_ctl0, pll->base + DIV_CTL0);

		writel_relaxed(FIELD_PREP(KDIV_MASK, rate.kdiv),
			       pll->base + DIV_CTL1);

		return 0;
	}

	/* Enable RST */
	gnrl_ctl = readl_relaxed(pll->base + GNRL_CTL);
	gnrl_ctl &= ~RST_MASK;
	writel_relaxed(gnrl_ctl, pll->base + GNRL_CTL);

	/* Enable BYPASS */
	gnrl_ctl |= BYPASS_MASK;
	writel_relaxed(gnrl_ctl, pll->base + GNRL_CTL);

	div_ctl0 = FIELD_PREP(MDIV_MASK, rate.mdiv) |
		   FIELD_PREP(PDIV_MASK, rate.pdiv) |
		   FIELD_PREP(SDIV_MASK, rate.sdiv);
	writel_relaxed(div_ctl0, pll->base + DIV_CTL0);

	writel_relaxed(FIELD_PREP(KDIV_MASK, rate.kdiv), pll->base + DIV_CTL1);

	/*
	 * According to SPEC, t3 - t2 need to be greater than
	 * 1us and 1/FREF, respectively.
	 * FREF is FIN / Prediv, the prediv is [1, 63], so choose
	 * 3us.
	 */
	udelay(3);

	/* Disable RST */
	gnrl_ctl |= RST_MASK;
	writel_relaxed(gnrl_ctl, pll->base + GNRL_CTL);

	/* Wait Lock*/
	ret = clk_pll14xx_wait_lock(pll);
	if (ret)
		return ret;

	/* Bypass */
	gnrl_ctl &= ~BYPASS_MASK;
	writel_relaxed(gnrl_ctl, pll->base + GNRL_CTL);

	return 0;
}

static int clk_pll14xx_prepare(struct clk_hw *hw)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 val;
	int ret;

	/*
	 * RESETB = 1 from 0, PLL starts its normal
	 * operation after lock time
	 */
	val = readl_relaxed(pll->base + GNRL_CTL);
	if (val & RST_MASK)
		return 0;
	val |= BYPASS_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);
	val |= RST_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);

	ret = clk_pll14xx_wait_lock(pll);
	if (ret)
		return ret;

	val &= ~BYPASS_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);

	return 0;
}

static int clk_pll14xx_is_prepared(struct clk_hw *hw)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 val;

	val = readl_relaxed(pll->base + GNRL_CTL);

	return (val & RST_MASK) ? 1 : 0;
}

static void clk_pll14xx_unprepare(struct clk_hw *hw)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 val;

	/*
	 * Set RST to 0, power down mode is enabled and
	 * every digital block is reset
	 */
	val = readl_relaxed(pll->base + GNRL_CTL);
	val &= ~RST_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);
}

static const struct clk_ops clk_pll1416x_ops = {
	.prepare	= clk_pll14xx_prepare,
	.unprepare	= clk_pll14xx_unprepare,
	.is_prepared	= clk_pll14xx_is_prepared,
	.recalc_rate	= clk_pll14xx_recalc_rate,
	.round_rate	= clk_pll1416x_round_rate,
	.set_rate	= clk_pll1416x_set_rate,
};

static const struct clk_ops clk_pll1416x_min_ops = {
	.recalc_rate	= clk_pll14xx_recalc_rate,
};

static const struct clk_ops clk_pll1443x_ops = {
	.prepare	= clk_pll14xx_prepare,
	.unprepare	= clk_pll14xx_unprepare,
	.is_prepared	= clk_pll14xx_is_prepared,
	.recalc_rate	= clk_pll14xx_recalc_rate,
	.round_rate	= clk_pll1443x_round_rate,
	.set_rate	= clk_pll1443x_set_rate,
};

struct clk_hw *imx_dev_clk_hw_pll14xx(struct device *dev, const char *name,
				const char *parent_name, void __iomem *base,
				const struct imx_pll14xx_clk *pll_clk)
{
	struct clk_pll14xx *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;
	u32 val;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = pll_clk->flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	switch (pll_clk->type) {
	case PLL_1416X:
		if (!pll_clk->rate_table)
			init.ops = &clk_pll1416x_min_ops;
		else
			init.ops = &clk_pll1416x_ops;
		break;
	case PLL_1443X:
		init.ops = &clk_pll1443x_ops;
		break;
	default:
		pr_err("Unknown pll type for pll clk %s\n", name);
		kfree(pll);
		return ERR_PTR(-EINVAL);
	}

	pll->base = base;
	pll->hw.init = &init;
	pll->type = pll_clk->type;
	pll->rate_table = pll_clk->rate_table;
	pll->rate_count = pll_clk->rate_count;

	val = readl_relaxed(pll->base + GNRL_CTL);
	val &= ~BYPASS_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);

	hw = &pll->hw;

	ret = clk_hw_register(dev, hw);
	if (ret) {
		pr_err("failed to register pll %s %d\n", name, ret);
		kfree(pll);
		return ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(imx_dev_clk_hw_pll14xx);
