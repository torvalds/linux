// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2017-2018 NXP.
 */

#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/jiffies.h>

#include "clk.h"

#define GNRL_CTL	0x0
#define DIV_CTL		0x4
#define LOCK_STATUS	BIT(31)
#define LOCK_SEL_MASK	BIT(29)
#define CLKE_MASK	BIT(11)
#define RST_MASK	BIT(9)
#define BYPASS_MASK	BIT(4)
#define MDIV_SHIFT	12
#define MDIV_MASK	GENMASK(21, 12)
#define PDIV_SHIFT	4
#define PDIV_MASK	GENMASK(9, 4)
#define SDIV_SHIFT	0
#define SDIV_MASK	GENMASK(2, 0)
#define KDIV_SHIFT	0
#define KDIV_MASK	GENMASK(15, 0)

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
	PLL_1416X_RATE(600000000U,  300, 3, 2),
};

static const struct imx_pll14xx_rate_table imx_pll1443x_tbl[] = {
	PLL_1443X_RATE(650000000U, 325, 3, 2, 0),
	PLL_1443X_RATE(594000000U, 198, 2, 2, 0),
	PLL_1443X_RATE(393216000U, 262, 2, 3, 9437),
	PLL_1443X_RATE(361267200U, 361, 3, 3, 17511),
};

struct imx_pll14xx_clk imx_1443x_pll = {
	.type = PLL_1443X,
	.rate_table = imx_pll1443x_tbl,
	.rate_count = ARRAY_SIZE(imx_pll1443x_tbl),
};

struct imx_pll14xx_clk imx_1416x_pll = {
	.type = PLL_1416X,
	.rate_table = imx_pll1416x_tbl,
	.rate_count = ARRAY_SIZE(imx_pll1416x_tbl),
};

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

static long clk_pll14xx_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	const struct imx_pll14xx_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++)
		if (rate >= rate_table[i].rate)
			return rate_table[i].rate;

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

static unsigned long clk_pll1416x_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 mdiv, pdiv, sdiv, pll_div;
	u64 fvco = parent_rate;

	pll_div = readl_relaxed(pll->base + 4);
	mdiv = (pll_div & MDIV_MASK) >> MDIV_SHIFT;
	pdiv = (pll_div & PDIV_MASK) >> PDIV_SHIFT;
	sdiv = (pll_div & SDIV_MASK) >> SDIV_SHIFT;

	fvco *= mdiv;
	do_div(fvco, pdiv << sdiv);

	return fvco;
}

static unsigned long clk_pll1443x_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	u32 mdiv, pdiv, sdiv, pll_div_ctl0, pll_div_ctl1;
	short int kdiv;
	u64 fvco = parent_rate;

	pll_div_ctl0 = readl_relaxed(pll->base + 4);
	pll_div_ctl1 = readl_relaxed(pll->base + 8);
	mdiv = (pll_div_ctl0 & MDIV_MASK) >> MDIV_SHIFT;
	pdiv = (pll_div_ctl0 & PDIV_MASK) >> PDIV_SHIFT;
	sdiv = (pll_div_ctl0 & SDIV_MASK) >> SDIV_SHIFT;
	kdiv = pll_div_ctl1 & KDIV_MASK;

	/* fvco = (m * 65536 + k) * Fin / (p * 65536) */
	fvco *= (mdiv * 65536 + kdiv);
	pdiv *= 65536;

	do_div(fvco, pdiv << sdiv);

	return fvco;
}

static inline bool clk_pll14xx_mp_change(const struct imx_pll14xx_rate_table *rate,
					  u32 pll_div)
{
	u32 old_mdiv, old_pdiv;

	old_mdiv = (pll_div & MDIV_MASK) >> MDIV_SHIFT;
	old_pdiv = (pll_div & PDIV_MASK) >> PDIV_SHIFT;

	return rate->mdiv != old_mdiv || rate->pdiv != old_pdiv;
}

static int clk_pll14xx_wait_lock(struct clk_pll14xx *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base, val, val & LOCK_STATUS, 0,
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
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
		       drate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	tmp = readl_relaxed(pll->base + 4);

	if (!clk_pll14xx_mp_change(rate, tmp)) {
		tmp &= ~(SDIV_MASK) << SDIV_SHIFT;
		tmp |= rate->sdiv << SDIV_SHIFT;
		writel_relaxed(tmp, pll->base + 4);

		return 0;
	}

	/* Bypass clock and set lock to pll output lock */
	tmp = readl_relaxed(pll->base);
	tmp |= LOCK_SEL_MASK;
	writel_relaxed(tmp, pll->base);

	/* Enable RST */
	tmp &= ~RST_MASK;
	writel_relaxed(tmp, pll->base);

	/* Enable BYPASS */
	tmp |= BYPASS_MASK;
	writel(tmp, pll->base);

	div_val = (rate->mdiv << MDIV_SHIFT) | (rate->pdiv << PDIV_SHIFT) |
		(rate->sdiv << SDIV_SHIFT);
	writel_relaxed(div_val, pll->base + 0x4);

	/*
	 * According to SPEC, t3 - t2 need to be greater than
	 * 1us and 1/FREF, respectively.
	 * FREF is FIN / Prediv, the prediv is [1, 63], so choose
	 * 3us.
	 */
	udelay(3);

	/* Disable RST */
	tmp |= RST_MASK;
	writel_relaxed(tmp, pll->base);

	/* Wait Lock */
	ret = clk_pll14xx_wait_lock(pll);
	if (ret)
		return ret;

	/* Bypass */
	tmp &= ~BYPASS_MASK;
	writel_relaxed(tmp, pll->base);

	return 0;
}

static int clk_pll1443x_set_rate(struct clk_hw *hw, unsigned long drate,
				 unsigned long prate)
{
	struct clk_pll14xx *pll = to_clk_pll14xx(hw);
	const struct imx_pll14xx_rate_table *rate;
	u32 tmp, div_val;
	int ret;

	rate = imx_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	tmp = readl_relaxed(pll->base + 4);

	if (!clk_pll14xx_mp_change(rate, tmp)) {
		tmp &= ~(SDIV_MASK) << SDIV_SHIFT;
		tmp |= rate->sdiv << SDIV_SHIFT;
		writel_relaxed(tmp, pll->base + 4);

		tmp = rate->kdiv << KDIV_SHIFT;
		writel_relaxed(tmp, pll->base + 8);

		return 0;
	}

	/* Enable RST */
	tmp = readl_relaxed(pll->base);
	tmp &= ~RST_MASK;
	writel_relaxed(tmp, pll->base);

	/* Enable BYPASS */
	tmp |= BYPASS_MASK;
	writel_relaxed(tmp, pll->base);

	div_val = (rate->mdiv << MDIV_SHIFT) | (rate->pdiv << PDIV_SHIFT) |
		(rate->sdiv << SDIV_SHIFT);
	writel_relaxed(div_val, pll->base + 0x4);
	writel_relaxed(rate->kdiv << KDIV_SHIFT, pll->base + 0x8);

	/*
	 * According to SPEC, t3 - t2 need to be greater than
	 * 1us and 1/FREF, respectively.
	 * FREF is FIN / Prediv, the prediv is [1, 63], so choose
	 * 3us.
	 */
	udelay(3);

	/* Disable RST */
	tmp |= RST_MASK;
	writel_relaxed(tmp, pll->base);

	/* Wait Lock*/
	ret = clk_pll14xx_wait_lock(pll);
	if (ret)
		return ret;

	/* Bypass */
	tmp &= ~BYPASS_MASK;
	writel_relaxed(tmp, pll->base);

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
	.recalc_rate	= clk_pll1416x_recalc_rate,
	.round_rate	= clk_pll14xx_round_rate,
	.set_rate	= clk_pll1416x_set_rate,
};

static const struct clk_ops clk_pll1416x_min_ops = {
	.recalc_rate	= clk_pll1416x_recalc_rate,
};

static const struct clk_ops clk_pll1443x_ops = {
	.prepare	= clk_pll14xx_prepare,
	.unprepare	= clk_pll14xx_unprepare,
	.is_prepared	= clk_pll14xx_is_prepared,
	.recalc_rate	= clk_pll1443x_recalc_rate,
	.round_rate	= clk_pll14xx_round_rate,
	.set_rate	= clk_pll1443x_set_rate,
};

struct clk *imx_clk_pll14xx(const char *name, const char *parent_name,
			    void __iomem *base,
			    const struct imx_pll14xx_clk *pll_clk)
{
	struct clk_pll14xx *pll;
	struct clk *clk;
	struct clk_init_data init;
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
		pr_err("%s: Unknown pll type for pll clk %s\n",
		       __func__, name);
	};

	pll->base = base;
	pll->hw.init = &init;
	pll->type = pll_clk->type;
	pll->rate_table = pll_clk->rate_table;
	pll->rate_count = pll_clk->rate_count;

	val = readl_relaxed(pll->base + GNRL_CTL);
	val &= ~BYPASS_MASK;
	writel_relaxed(val, pll->base + GNRL_CTL);

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll %s %lu\n",
			__func__, name, PTR_ERR(clk));
		kfree(pll);
	}

	return clk;
}
