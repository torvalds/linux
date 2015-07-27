/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains the utility functions to register the pll clocks.
*/

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include "clk.h"
#include "clk-pll.h"

#define PLL_TIMEOUT_MS		10

struct samsung_clk_pll {
	struct clk_hw		hw;
	void __iomem		*lock_reg;
	void __iomem		*con_reg;
	enum samsung_pll_type	type;
	unsigned int		rate_count;
	const struct samsung_pll_rate_table *rate_table;
};

#define to_clk_pll(_hw) container_of(_hw, struct samsung_clk_pll, hw)

static const struct samsung_pll_rate_table *samsung_get_pll_settings(
				struct samsung_clk_pll *pll, unsigned long rate)
{
	const struct samsung_pll_rate_table  *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++) {
		if (rate == rate_table[i].rate)
			return &rate_table[i];
	}

	return NULL;
}

static long samsung_pll_round_rate(struct clk_hw *hw,
			unsigned long drate, unsigned long *prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate_table = pll->rate_table;
	int i;

	/* Assumming rate_table is in descending order */
	for (i = 0; i < pll->rate_count; i++) {
		if (drate >= rate_table[i].rate)
			return rate_table[i].rate;
	}

	/* return minimum supported value */
	return rate_table[i - 1].rate;
}

/*
 * PLL2126 Clock Type
 */

#define PLL2126_MDIV_MASK	(0xff)
#define PLL2126_PDIV_MASK	(0x3f)
#define PLL2126_SDIV_MASK	(0x3)
#define PLL2126_MDIV_SHIFT	(16)
#define PLL2126_PDIV_SHIFT	(8)
#define PLL2126_SDIV_SHIFT	(0)

static unsigned long samsung_pll2126_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 pll_con, mdiv, pdiv, sdiv;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL2126_MDIV_SHIFT) & PLL2126_MDIV_MASK;
	pdiv = (pll_con >> PLL2126_PDIV_SHIFT) & PLL2126_PDIV_MASK;
	sdiv = (pll_con >> PLL2126_SDIV_SHIFT) & PLL2126_SDIV_MASK;

	fvco *= (mdiv + 8);
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll2126_clk_ops = {
	.recalc_rate = samsung_pll2126_recalc_rate,
};

/*
 * PLL3000 Clock Type
 */

#define PLL3000_MDIV_MASK	(0xff)
#define PLL3000_PDIV_MASK	(0x3)
#define PLL3000_SDIV_MASK	(0x3)
#define PLL3000_MDIV_SHIFT	(16)
#define PLL3000_PDIV_SHIFT	(8)
#define PLL3000_SDIV_SHIFT	(0)

static unsigned long samsung_pll3000_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 pll_con, mdiv, pdiv, sdiv;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL3000_MDIV_SHIFT) & PLL3000_MDIV_MASK;
	pdiv = (pll_con >> PLL3000_PDIV_SHIFT) & PLL3000_PDIV_MASK;
	sdiv = (pll_con >> PLL3000_SDIV_SHIFT) & PLL3000_SDIV_MASK;

	fvco *= (2 * (mdiv + 8));
	do_div(fvco, pdiv << sdiv);

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll3000_clk_ops = {
	.recalc_rate = samsung_pll3000_recalc_rate,
};

/*
 * PLL35xx Clock Type
 */
/* Maximum lock time can be 270 * PDIV cycles */
#define PLL35XX_LOCK_FACTOR	(270)

#define PLL35XX_MDIV_MASK       (0x3FF)
#define PLL35XX_PDIV_MASK       (0x3F)
#define PLL35XX_SDIV_MASK       (0x7)
#define PLL35XX_LOCK_STAT_MASK	(0x1)
#define PLL35XX_MDIV_SHIFT      (16)
#define PLL35XX_PDIV_SHIFT      (8)
#define PLL35XX_SDIV_SHIFT      (0)
#define PLL35XX_LOCK_STAT_SHIFT	(29)

static unsigned long samsung_pll35xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL35XX_MDIV_SHIFT) & PLL35XX_MDIV_MASK;
	pdiv = (pll_con >> PLL35XX_PDIV_SHIFT) & PLL35XX_PDIV_MASK;
	sdiv = (pll_con >> PLL35XX_SDIV_SHIFT) & PLL35XX_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static inline bool samsung_pll35xx_mp_change(
		const struct samsung_pll_rate_table *rate, u32 pll_con)
{
	u32 old_mdiv, old_pdiv;

	old_mdiv = (pll_con >> PLL35XX_MDIV_SHIFT) & PLL35XX_MDIV_MASK;
	old_pdiv = (pll_con >> PLL35XX_PDIV_SHIFT) & PLL35XX_PDIV_MASK;

	return (rate->mdiv != old_mdiv || rate->pdiv != old_pdiv);
}

static int samsung_pll35xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate;
	u32 tmp;

	/* Get required rate settings from table */
	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	tmp = __raw_readl(pll->con_reg);

	if (!(samsung_pll35xx_mp_change(rate, tmp))) {
		/* If only s change, change just s value only*/
		tmp &= ~(PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT);
		tmp |= rate->sdiv << PLL35XX_SDIV_SHIFT;
		__raw_writel(tmp, pll->con_reg);

		return 0;
	}

	/* Set PLL lock time. */
	__raw_writel(rate->pdiv * PLL35XX_LOCK_FACTOR,
			pll->lock_reg);

	/* Change PLL PMS values */
	tmp &= ~((PLL35XX_MDIV_MASK << PLL35XX_MDIV_SHIFT) |
			(PLL35XX_PDIV_MASK << PLL35XX_PDIV_SHIFT) |
			(PLL35XX_SDIV_MASK << PLL35XX_SDIV_SHIFT));
	tmp |= (rate->mdiv << PLL35XX_MDIV_SHIFT) |
			(rate->pdiv << PLL35XX_PDIV_SHIFT) |
			(rate->sdiv << PLL35XX_SDIV_SHIFT);
	__raw_writel(tmp, pll->con_reg);

	/* wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(pll->con_reg);
	} while (!(tmp & (PLL35XX_LOCK_STAT_MASK
				<< PLL35XX_LOCK_STAT_SHIFT)));
	return 0;
}

static const struct clk_ops samsung_pll35xx_clk_ops = {
	.recalc_rate = samsung_pll35xx_recalc_rate,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_pll35xx_set_rate,
};

static const struct clk_ops samsung_pll35xx_clk_min_ops = {
	.recalc_rate = samsung_pll35xx_recalc_rate,
};

/*
 * PLL36xx Clock Type
 */
/* Maximum lock time can be 3000 * PDIV cycles */
#define PLL36XX_LOCK_FACTOR    (3000)

#define PLL36XX_KDIV_MASK	(0xFFFF)
#define PLL36XX_MDIV_MASK	(0x1FF)
#define PLL36XX_PDIV_MASK	(0x3F)
#define PLL36XX_SDIV_MASK	(0x7)
#define PLL36XX_MDIV_SHIFT	(16)
#define PLL36XX_PDIV_SHIFT	(8)
#define PLL36XX_SDIV_SHIFT	(0)
#define PLL36XX_KDIV_SHIFT	(0)
#define PLL36XX_LOCK_STAT_SHIFT	(29)

static unsigned long samsung_pll36xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con0, pll_con1;
	s16 kdiv;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 4);
	mdiv = (pll_con0 >> PLL36XX_MDIV_SHIFT) & PLL36XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL36XX_PDIV_SHIFT) & PLL36XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL36XX_SDIV_SHIFT) & PLL36XX_SDIV_MASK;
	kdiv = (s16)(pll_con1 & PLL36XX_KDIV_MASK);

	fvco *= (mdiv << 16) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= 16;

	return (unsigned long)fvco;
}

static inline bool samsung_pll36xx_mpk_change(
	const struct samsung_pll_rate_table *rate, u32 pll_con0, u32 pll_con1)
{
	u32 old_mdiv, old_pdiv, old_kdiv;

	old_mdiv = (pll_con0 >> PLL36XX_MDIV_SHIFT) & PLL36XX_MDIV_MASK;
	old_pdiv = (pll_con0 >> PLL36XX_PDIV_SHIFT) & PLL36XX_PDIV_MASK;
	old_kdiv = (pll_con1 >> PLL36XX_KDIV_SHIFT) & PLL36XX_KDIV_MASK;

	return (rate->mdiv != old_mdiv || rate->pdiv != old_pdiv ||
		rate->kdiv != old_kdiv);
}

static int samsung_pll36xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 tmp, pll_con0, pll_con1;
	const struct samsung_pll_rate_table *rate;

	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 4);

	if (!(samsung_pll36xx_mpk_change(rate, pll_con0, pll_con1))) {
		/* If only s change, change just s value only*/
		pll_con0 &= ~(PLL36XX_SDIV_MASK << PLL36XX_SDIV_SHIFT);
		pll_con0 |= (rate->sdiv << PLL36XX_SDIV_SHIFT);
		__raw_writel(pll_con0, pll->con_reg);

		return 0;
	}

	/* Set PLL lock time. */
	__raw_writel(rate->pdiv * PLL36XX_LOCK_FACTOR, pll->lock_reg);

	 /* Change PLL PMS values */
	pll_con0 &= ~((PLL36XX_MDIV_MASK << PLL36XX_MDIV_SHIFT) |
			(PLL36XX_PDIV_MASK << PLL36XX_PDIV_SHIFT) |
			(PLL36XX_SDIV_MASK << PLL36XX_SDIV_SHIFT));
	pll_con0 |= (rate->mdiv << PLL36XX_MDIV_SHIFT) |
			(rate->pdiv << PLL36XX_PDIV_SHIFT) |
			(rate->sdiv << PLL36XX_SDIV_SHIFT);
	__raw_writel(pll_con0, pll->con_reg);

	pll_con1 &= ~(PLL36XX_KDIV_MASK << PLL36XX_KDIV_SHIFT);
	pll_con1 |= rate->kdiv << PLL36XX_KDIV_SHIFT;
	__raw_writel(pll_con1, pll->con_reg + 4);

	/* wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(pll->con_reg);
	} while (!(tmp & (1 << PLL36XX_LOCK_STAT_SHIFT)));

	return 0;
}

static const struct clk_ops samsung_pll36xx_clk_ops = {
	.recalc_rate = samsung_pll36xx_recalc_rate,
	.set_rate = samsung_pll36xx_set_rate,
	.round_rate = samsung_pll_round_rate,
};

static const struct clk_ops samsung_pll36xx_clk_min_ops = {
	.recalc_rate = samsung_pll36xx_recalc_rate,
};

/*
 * PLL45xx Clock Type
 */
#define PLL4502_LOCK_FACTOR	400
#define PLL4508_LOCK_FACTOR	240

#define PLL45XX_MDIV_MASK	(0x3FF)
#define PLL45XX_PDIV_MASK	(0x3F)
#define PLL45XX_SDIV_MASK	(0x7)
#define PLL45XX_AFC_MASK	(0x1F)
#define PLL45XX_MDIV_SHIFT	(16)
#define PLL45XX_PDIV_SHIFT	(8)
#define PLL45XX_SDIV_SHIFT	(0)
#define PLL45XX_AFC_SHIFT	(0)

#define PLL45XX_ENABLE		BIT(31)
#define PLL45XX_LOCKED		BIT(29)

static unsigned long samsung_pll45xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL45XX_MDIV_SHIFT) & PLL45XX_MDIV_MASK;
	pdiv = (pll_con >> PLL45XX_PDIV_SHIFT) & PLL45XX_PDIV_MASK;
	sdiv = (pll_con >> PLL45XX_SDIV_SHIFT) & PLL45XX_SDIV_MASK;

	if (pll->type == pll_4508)
		sdiv = sdiv - 1;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static bool samsung_pll45xx_mp_change(u32 pll_con0, u32 pll_con1,
				const struct samsung_pll_rate_table *rate)
{
	u32 old_mdiv, old_pdiv, old_afc;

	old_mdiv = (pll_con0 >> PLL45XX_MDIV_SHIFT) & PLL45XX_MDIV_MASK;
	old_pdiv = (pll_con0 >> PLL45XX_PDIV_SHIFT) & PLL45XX_PDIV_MASK;
	old_afc = (pll_con1 >> PLL45XX_AFC_SHIFT) & PLL45XX_AFC_MASK;

	return (old_mdiv != rate->mdiv || old_pdiv != rate->pdiv
		|| old_afc != rate->afc);
}

static int samsung_pll45xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate;
	u32 con0, con1;
	ktime_t start;

	/* Get required rate settings from table */
	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	con0 = __raw_readl(pll->con_reg);
	con1 = __raw_readl(pll->con_reg + 0x4);

	if (!(samsung_pll45xx_mp_change(con0, con1, rate))) {
		/* If only s change, change just s value only*/
		con0 &= ~(PLL45XX_SDIV_MASK << PLL45XX_SDIV_SHIFT);
		con0 |= rate->sdiv << PLL45XX_SDIV_SHIFT;
		__raw_writel(con0, pll->con_reg);

		return 0;
	}

	/* Set PLL PMS values. */
	con0 &= ~((PLL45XX_MDIV_MASK << PLL45XX_MDIV_SHIFT) |
			(PLL45XX_PDIV_MASK << PLL45XX_PDIV_SHIFT) |
			(PLL45XX_SDIV_MASK << PLL45XX_SDIV_SHIFT));
	con0 |= (rate->mdiv << PLL45XX_MDIV_SHIFT) |
			(rate->pdiv << PLL45XX_PDIV_SHIFT) |
			(rate->sdiv << PLL45XX_SDIV_SHIFT);

	/* Set PLL AFC value. */
	con1 = __raw_readl(pll->con_reg + 0x4);
	con1 &= ~(PLL45XX_AFC_MASK << PLL45XX_AFC_SHIFT);
	con1 |= (rate->afc << PLL45XX_AFC_SHIFT);

	/* Set PLL lock time. */
	switch (pll->type) {
	case pll_4502:
		__raw_writel(rate->pdiv * PLL4502_LOCK_FACTOR, pll->lock_reg);
		break;
	case pll_4508:
		__raw_writel(rate->pdiv * PLL4508_LOCK_FACTOR, pll->lock_reg);
		break;
	default:
		break;
	}

	/* Set new configuration. */
	__raw_writel(con1, pll->con_reg + 0x4);
	__raw_writel(con0, pll->con_reg);

	/* Wait for locking. */
	start = ktime_get();
	while (!(__raw_readl(pll->con_reg) & PLL45XX_LOCKED)) {
		ktime_t delta = ktime_sub(ktime_get(), start);

		if (ktime_to_ms(delta) > PLL_TIMEOUT_MS) {
			pr_err("%s: could not lock PLL %s\n",
					__func__, __clk_get_name(hw->clk));
			return -EFAULT;
		}

		cpu_relax();
	}

	return 0;
}

static const struct clk_ops samsung_pll45xx_clk_ops = {
	.recalc_rate = samsung_pll45xx_recalc_rate,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_pll45xx_set_rate,
};

static const struct clk_ops samsung_pll45xx_clk_min_ops = {
	.recalc_rate = samsung_pll45xx_recalc_rate,
};

/*
 * PLL46xx Clock Type
 */
#define PLL46XX_LOCK_FACTOR	3000

#define PLL46XX_VSEL_MASK	(1)
#define PLL46XX_MDIV_MASK	(0x1FF)
#define PLL1460X_MDIV_MASK	(0x3FF)

#define PLL46XX_PDIV_MASK	(0x3F)
#define PLL46XX_SDIV_MASK	(0x7)
#define PLL46XX_VSEL_SHIFT	(27)
#define PLL46XX_MDIV_SHIFT	(16)
#define PLL46XX_PDIV_SHIFT	(8)
#define PLL46XX_SDIV_SHIFT	(0)

#define PLL46XX_KDIV_MASK	(0xFFFF)
#define PLL4650C_KDIV_MASK	(0xFFF)
#define PLL46XX_KDIV_SHIFT	(0)
#define PLL46XX_MFR_MASK	(0x3F)
#define PLL46XX_MRR_MASK	(0x1F)
#define PLL46XX_KDIV_SHIFT	(0)
#define PLL46XX_MFR_SHIFT	(16)
#define PLL46XX_MRR_SHIFT	(24)

#define PLL46XX_ENABLE		BIT(31)
#define PLL46XX_LOCKED		BIT(29)
#define PLL46XX_VSEL		BIT(27)

static unsigned long samsung_pll46xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, kdiv, pll_con0, pll_con1, shift;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 4);
	mdiv = (pll_con0 >> PLL46XX_MDIV_SHIFT) & ((pll->type == pll_1460x) ?
				PLL1460X_MDIV_MASK : PLL46XX_MDIV_MASK);
	pdiv = (pll_con0 >> PLL46XX_PDIV_SHIFT) & PLL46XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL46XX_SDIV_SHIFT) & PLL46XX_SDIV_MASK;
	kdiv = pll->type == pll_4650c ? pll_con1 & PLL4650C_KDIV_MASK :
					pll_con1 & PLL46XX_KDIV_MASK;

	shift = ((pll->type == pll_4600) || (pll->type == pll_1460x)) ? 16 : 10;

	fvco *= (mdiv << shift) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= shift;

	return (unsigned long)fvco;
}

static bool samsung_pll46xx_mpk_change(u32 pll_con0, u32 pll_con1,
				const struct samsung_pll_rate_table *rate)
{
	u32 old_mdiv, old_pdiv, old_kdiv;

	old_mdiv = (pll_con0 >> PLL46XX_MDIV_SHIFT) & PLL46XX_MDIV_MASK;
	old_pdiv = (pll_con0 >> PLL46XX_PDIV_SHIFT) & PLL46XX_PDIV_MASK;
	old_kdiv = (pll_con1 >> PLL46XX_KDIV_SHIFT) & PLL46XX_KDIV_MASK;

	return (old_mdiv != rate->mdiv || old_pdiv != rate->pdiv
		|| old_kdiv != rate->kdiv);
}

static int samsung_pll46xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate;
	u32 con0, con1, lock;
	ktime_t start;

	/* Get required rate settings from table */
	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	con0 = __raw_readl(pll->con_reg);
	con1 = __raw_readl(pll->con_reg + 0x4);

	if (!(samsung_pll46xx_mpk_change(con0, con1, rate))) {
		/* If only s change, change just s value only*/
		con0 &= ~(PLL46XX_SDIV_MASK << PLL46XX_SDIV_SHIFT);
		con0 |= rate->sdiv << PLL46XX_SDIV_SHIFT;
		__raw_writel(con0, pll->con_reg);

		return 0;
	}

	/* Set PLL lock time. */
	lock = rate->pdiv * PLL46XX_LOCK_FACTOR;
	if (lock > 0xffff)
		/* Maximum lock time bitfield is 16-bit. */
		lock = 0xffff;

	/* Set PLL PMS and VSEL values. */
	if (pll->type == pll_1460x) {
		con0 &= ~((PLL1460X_MDIV_MASK << PLL46XX_MDIV_SHIFT) |
			(PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT) |
			(PLL46XX_SDIV_MASK << PLL46XX_SDIV_SHIFT));
	} else {
		con0 &= ~((PLL46XX_MDIV_MASK << PLL46XX_MDIV_SHIFT) |
			(PLL46XX_PDIV_MASK << PLL46XX_PDIV_SHIFT) |
			(PLL46XX_SDIV_MASK << PLL46XX_SDIV_SHIFT) |
			(PLL46XX_VSEL_MASK << PLL46XX_VSEL_SHIFT));
		con0 |=	rate->vsel << PLL46XX_VSEL_SHIFT;
	}

	con0 |= (rate->mdiv << PLL46XX_MDIV_SHIFT) |
			(rate->pdiv << PLL46XX_PDIV_SHIFT) |
			(rate->sdiv << PLL46XX_SDIV_SHIFT);

	/* Set PLL K, MFR and MRR values. */
	con1 = __raw_readl(pll->con_reg + 0x4);
	con1 &= ~((PLL46XX_KDIV_MASK << PLL46XX_KDIV_SHIFT) |
			(PLL46XX_MFR_MASK << PLL46XX_MFR_SHIFT) |
			(PLL46XX_MRR_MASK << PLL46XX_MRR_SHIFT));
	con1 |= (rate->kdiv << PLL46XX_KDIV_SHIFT) |
			(rate->mfr << PLL46XX_MFR_SHIFT) |
			(rate->mrr << PLL46XX_MRR_SHIFT);

	/* Write configuration to PLL */
	__raw_writel(lock, pll->lock_reg);
	__raw_writel(con0, pll->con_reg);
	__raw_writel(con1, pll->con_reg + 0x4);

	/* Wait for locking. */
	start = ktime_get();
	while (!(__raw_readl(pll->con_reg) & PLL46XX_LOCKED)) {
		ktime_t delta = ktime_sub(ktime_get(), start);

		if (ktime_to_ms(delta) > PLL_TIMEOUT_MS) {
			pr_err("%s: could not lock PLL %s\n",
					__func__, __clk_get_name(hw->clk));
			return -EFAULT;
		}

		cpu_relax();
	}

	return 0;
}

static const struct clk_ops samsung_pll46xx_clk_ops = {
	.recalc_rate = samsung_pll46xx_recalc_rate,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_pll46xx_set_rate,
};

static const struct clk_ops samsung_pll46xx_clk_min_ops = {
	.recalc_rate = samsung_pll46xx_recalc_rate,
};

/*
 * PLL6552 Clock Type
 */

#define PLL6552_MDIV_MASK	0x3ff
#define PLL6552_PDIV_MASK	0x3f
#define PLL6552_SDIV_MASK	0x7
#define PLL6552_MDIV_SHIFT	16
#define PLL6552_MDIV_SHIFT_2416	14
#define PLL6552_PDIV_SHIFT	8
#define PLL6552_PDIV_SHIFT_2416	5
#define PLL6552_SDIV_SHIFT	0

static unsigned long samsung_pll6552_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	if (pll->type == pll_6552_s3c2416) {
		mdiv = (pll_con >> PLL6552_MDIV_SHIFT_2416) & PLL6552_MDIV_MASK;
		pdiv = (pll_con >> PLL6552_PDIV_SHIFT_2416) & PLL6552_PDIV_MASK;
	} else {
		mdiv = (pll_con >> PLL6552_MDIV_SHIFT) & PLL6552_MDIV_MASK;
		pdiv = (pll_con >> PLL6552_PDIV_SHIFT) & PLL6552_PDIV_MASK;
	}
	sdiv = (pll_con >> PLL6552_SDIV_SHIFT) & PLL6552_SDIV_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll6552_clk_ops = {
	.recalc_rate = samsung_pll6552_recalc_rate,
};

/*
 * PLL6553 Clock Type
 */

#define PLL6553_MDIV_MASK	0xff
#define PLL6553_PDIV_MASK	0x3f
#define PLL6553_SDIV_MASK	0x7
#define PLL6553_KDIV_MASK	0xffff
#define PLL6553_MDIV_SHIFT	16
#define PLL6553_PDIV_SHIFT	8
#define PLL6553_SDIV_SHIFT	0
#define PLL6553_KDIV_SHIFT	0

static unsigned long samsung_pll6553_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, kdiv, pll_con0, pll_con1;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con1 = __raw_readl(pll->con_reg + 0x4);
	mdiv = (pll_con0 >> PLL6553_MDIV_SHIFT) & PLL6553_MDIV_MASK;
	pdiv = (pll_con0 >> PLL6553_PDIV_SHIFT) & PLL6553_PDIV_MASK;
	sdiv = (pll_con0 >> PLL6553_SDIV_SHIFT) & PLL6553_SDIV_MASK;
	kdiv = (pll_con1 >> PLL6553_KDIV_SHIFT) & PLL6553_KDIV_MASK;

	fvco *= (mdiv << 16) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= 16;

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll6553_clk_ops = {
	.recalc_rate = samsung_pll6553_recalc_rate,
};

/*
 * PLL Clock Type of S3C24XX before S3C2443
 */

#define PLLS3C2410_MDIV_MASK		(0xff)
#define PLLS3C2410_PDIV_MASK		(0x1f)
#define PLLS3C2410_SDIV_MASK		(0x3)
#define PLLS3C2410_MDIV_SHIFT		(12)
#define PLLS3C2410_PDIV_SHIFT		(4)
#define PLLS3C2410_SDIV_SHIFT		(0)

#define PLLS3C2410_ENABLE_REG_OFFSET	0x10

static unsigned long samsung_s3c2410_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 pll_con, mdiv, pdiv, sdiv;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLLS3C2410_MDIV_SHIFT) & PLLS3C2410_MDIV_MASK;
	pdiv = (pll_con >> PLLS3C2410_PDIV_SHIFT) & PLLS3C2410_PDIV_MASK;
	sdiv = (pll_con >> PLLS3C2410_SDIV_SHIFT) & PLLS3C2410_SDIV_MASK;

	fvco *= (mdiv + 8);
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned int)fvco;
}

static unsigned long samsung_s3c2440_mpll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 pll_con, mdiv, pdiv, sdiv;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLLS3C2410_MDIV_SHIFT) & PLLS3C2410_MDIV_MASK;
	pdiv = (pll_con >> PLLS3C2410_PDIV_SHIFT) & PLLS3C2410_PDIV_MASK;
	sdiv = (pll_con >> PLLS3C2410_SDIV_SHIFT) & PLLS3C2410_SDIV_MASK;

	fvco *= (2 * (mdiv + 8));
	do_div(fvco, (pdiv + 2) << sdiv);

	return (unsigned int)fvco;
}

static int samsung_s3c2410_pll_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate;
	u32 tmp;

	/* Get required rate settings from table */
	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	tmp = __raw_readl(pll->con_reg);

	/* Change PLL PMS values */
	tmp &= ~((PLLS3C2410_MDIV_MASK << PLLS3C2410_MDIV_SHIFT) |
			(PLLS3C2410_PDIV_MASK << PLLS3C2410_PDIV_SHIFT) |
			(PLLS3C2410_SDIV_MASK << PLLS3C2410_SDIV_SHIFT));
	tmp |= (rate->mdiv << PLLS3C2410_MDIV_SHIFT) |
			(rate->pdiv << PLLS3C2410_PDIV_SHIFT) |
			(rate->sdiv << PLLS3C2410_SDIV_SHIFT);
	__raw_writel(tmp, pll->con_reg);

	/* Time to settle according to the manual */
	udelay(300);

	return 0;
}

static int samsung_s3c2410_pll_enable(struct clk_hw *hw, int bit, bool enable)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 pll_en = __raw_readl(pll->lock_reg + PLLS3C2410_ENABLE_REG_OFFSET);
	u32 pll_en_orig = pll_en;

	if (enable)
		pll_en &= ~BIT(bit);
	else
		pll_en |= BIT(bit);

	__raw_writel(pll_en, pll->lock_reg + PLLS3C2410_ENABLE_REG_OFFSET);

	/* if we started the UPLL, then allow to settle */
	if (enable && (pll_en_orig & BIT(bit)))
		udelay(300);

	return 0;
}

static int samsung_s3c2410_mpll_enable(struct clk_hw *hw)
{
	return samsung_s3c2410_pll_enable(hw, 5, true);
}

static void samsung_s3c2410_mpll_disable(struct clk_hw *hw)
{
	samsung_s3c2410_pll_enable(hw, 5, false);
}

static int samsung_s3c2410_upll_enable(struct clk_hw *hw)
{
	return samsung_s3c2410_pll_enable(hw, 7, true);
}

static void samsung_s3c2410_upll_disable(struct clk_hw *hw)
{
	samsung_s3c2410_pll_enable(hw, 7, false);
}

static const struct clk_ops samsung_s3c2410_mpll_clk_min_ops = {
	.recalc_rate = samsung_s3c2410_pll_recalc_rate,
	.enable = samsung_s3c2410_mpll_enable,
	.disable = samsung_s3c2410_mpll_disable,
};

static const struct clk_ops samsung_s3c2410_upll_clk_min_ops = {
	.recalc_rate = samsung_s3c2410_pll_recalc_rate,
	.enable = samsung_s3c2410_upll_enable,
	.disable = samsung_s3c2410_upll_disable,
};

static const struct clk_ops samsung_s3c2440_mpll_clk_min_ops = {
	.recalc_rate = samsung_s3c2440_mpll_recalc_rate,
	.enable = samsung_s3c2410_mpll_enable,
	.disable = samsung_s3c2410_mpll_disable,
};

static const struct clk_ops samsung_s3c2410_mpll_clk_ops = {
	.recalc_rate = samsung_s3c2410_pll_recalc_rate,
	.enable = samsung_s3c2410_mpll_enable,
	.disable = samsung_s3c2410_mpll_disable,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_s3c2410_pll_set_rate,
};

static const struct clk_ops samsung_s3c2410_upll_clk_ops = {
	.recalc_rate = samsung_s3c2410_pll_recalc_rate,
	.enable = samsung_s3c2410_upll_enable,
	.disable = samsung_s3c2410_upll_disable,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_s3c2410_pll_set_rate,
};

static const struct clk_ops samsung_s3c2440_mpll_clk_ops = {
	.recalc_rate = samsung_s3c2440_mpll_recalc_rate,
	.enable = samsung_s3c2410_mpll_enable,
	.disable = samsung_s3c2410_mpll_disable,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_s3c2410_pll_set_rate,
};

/*
 * PLL2550x Clock Type
 */

#define PLL2550X_R_MASK       (0x1)
#define PLL2550X_P_MASK       (0x3F)
#define PLL2550X_M_MASK       (0x3FF)
#define PLL2550X_S_MASK       (0x7)
#define PLL2550X_R_SHIFT      (20)
#define PLL2550X_P_SHIFT      (14)
#define PLL2550X_M_SHIFT      (4)
#define PLL2550X_S_SHIFT      (0)

struct samsung_clk_pll2550x {
	struct clk_hw		hw;
	const void __iomem	*reg_base;
	unsigned long		offset;
};

#define to_clk_pll2550x(_hw) container_of(_hw, struct samsung_clk_pll2550x, hw)

static unsigned long samsung_pll2550x_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll2550x *pll = to_clk_pll2550x(hw);
	u32 r, p, m, s, pll_stat;
	u64 fvco = parent_rate;

	pll_stat = __raw_readl(pll->reg_base + pll->offset * 3);
	r = (pll_stat >> PLL2550X_R_SHIFT) & PLL2550X_R_MASK;
	if (!r)
		return 0;
	p = (pll_stat >> PLL2550X_P_SHIFT) & PLL2550X_P_MASK;
	m = (pll_stat >> PLL2550X_M_SHIFT) & PLL2550X_M_MASK;
	s = (pll_stat >> PLL2550X_S_SHIFT) & PLL2550X_S_MASK;

	fvco *= m;
	do_div(fvco, (p << s));

	return (unsigned long)fvco;
}

static const struct clk_ops samsung_pll2550x_clk_ops = {
	.recalc_rate = samsung_pll2550x_recalc_rate,
};

struct clk * __init samsung_clk_register_pll2550x(const char *name,
			const char *pname, const void __iomem *reg_base,
			const unsigned long offset)
{
	struct samsung_clk_pll2550x *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n", __func__, name);
		return NULL;
	}

	init.name = name;
	init.ops = &samsung_pll2550x_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;
	init.parent_names = &pname;
	init.num_parents = 1;

	pll->hw.init = &init;
	pll->reg_base = reg_base;
	pll->offset = offset;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s\n", __func__,
				name);
		kfree(pll);
	}

	if (clk_register_clkdev(clk, name, NULL))
		pr_err("%s: failed to register lookup for %s", __func__, name);

	return clk;
}

/*
 * PLL2550xx Clock Type
 */

/* Maximum lock time can be 270 * PDIV cycles */
#define PLL2550XX_LOCK_FACTOR 270

#define PLL2550XX_M_MASK		0x3FF
#define PLL2550XX_P_MASK		0x3F
#define PLL2550XX_S_MASK		0x7
#define PLL2550XX_LOCK_STAT_MASK	0x1
#define PLL2550XX_M_SHIFT		9
#define PLL2550XX_P_SHIFT		3
#define PLL2550XX_S_SHIFT		0
#define PLL2550XX_LOCK_STAT_SHIFT	21

static unsigned long samsung_pll2550xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con;
	u64 fvco = parent_rate;

	pll_con = __raw_readl(pll->con_reg);
	mdiv = (pll_con >> PLL2550XX_M_SHIFT) & PLL2550XX_M_MASK;
	pdiv = (pll_con >> PLL2550XX_P_SHIFT) & PLL2550XX_P_MASK;
	sdiv = (pll_con >> PLL2550XX_S_SHIFT) & PLL2550XX_S_MASK;

	fvco *= mdiv;
	do_div(fvco, (pdiv << sdiv));

	return (unsigned long)fvco;
}

static inline bool samsung_pll2550xx_mp_change(u32 mdiv, u32 pdiv, u32 pll_con)
{
	u32 old_mdiv, old_pdiv;

	old_mdiv = (pll_con >> PLL2550XX_M_SHIFT) & PLL2550XX_M_MASK;
	old_pdiv = (pll_con >> PLL2550XX_P_SHIFT) & PLL2550XX_P_MASK;

	return mdiv != old_mdiv || pdiv != old_pdiv;
}

static int samsung_pll2550xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long prate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	const struct samsung_pll_rate_table *rate;
	u32 tmp;

	/* Get required rate settings from table */
	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	tmp = __raw_readl(pll->con_reg);

	if (!(samsung_pll2550xx_mp_change(rate->mdiv, rate->pdiv, tmp))) {
		/* If only s change, change just s value only*/
		tmp &= ~(PLL2550XX_S_MASK << PLL2550XX_S_SHIFT);
		tmp |= rate->sdiv << PLL2550XX_S_SHIFT;
		__raw_writel(tmp, pll->con_reg);

		return 0;
	}

	/* Set PLL lock time. */
	__raw_writel(rate->pdiv * PLL2550XX_LOCK_FACTOR, pll->lock_reg);

	/* Change PLL PMS values */
	tmp &= ~((PLL2550XX_M_MASK << PLL2550XX_M_SHIFT) |
			(PLL2550XX_P_MASK << PLL2550XX_P_SHIFT) |
			(PLL2550XX_S_MASK << PLL2550XX_S_SHIFT));
	tmp |= (rate->mdiv << PLL2550XX_M_SHIFT) |
			(rate->pdiv << PLL2550XX_P_SHIFT) |
			(rate->sdiv << PLL2550XX_S_SHIFT);
	__raw_writel(tmp, pll->con_reg);

	/* wait_lock_time */
	do {
		cpu_relax();
		tmp = __raw_readl(pll->con_reg);
	} while (!(tmp & (PLL2550XX_LOCK_STAT_MASK
			<< PLL2550XX_LOCK_STAT_SHIFT)));

	return 0;
}

static const struct clk_ops samsung_pll2550xx_clk_ops = {
	.recalc_rate = samsung_pll2550xx_recalc_rate,
	.round_rate = samsung_pll_round_rate,
	.set_rate = samsung_pll2550xx_set_rate,
};

static const struct clk_ops samsung_pll2550xx_clk_min_ops = {
	.recalc_rate = samsung_pll2550xx_recalc_rate,
};

/*
 * PLL2650XX Clock Type
 */

/* Maximum lock time can be 3000 * PDIV cycles */
#define PLL2650XX_LOCK_FACTOR 3000

#define PLL2650XX_MDIV_SHIFT		9
#define PLL2650XX_PDIV_SHIFT		3
#define PLL2650XX_SDIV_SHIFT		0
#define PLL2650XX_KDIV_SHIFT		0
#define PLL2650XX_MDIV_MASK		0x1ff
#define PLL2650XX_PDIV_MASK		0x3f
#define PLL2650XX_SDIV_MASK		0x7
#define PLL2650XX_KDIV_MASK		0xffff
#define PLL2650XX_PLL_ENABLE_SHIFT	23
#define PLL2650XX_PLL_LOCKTIME_SHIFT	21
#define PLL2650XX_PLL_FOUTMASK_SHIFT	31

static unsigned long samsung_pll2650xx_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 mdiv, pdiv, sdiv, pll_con0, pll_con2;
	s16 kdiv;
	u64 fvco = parent_rate;

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con2 = __raw_readl(pll->con_reg + 8);
	mdiv = (pll_con0 >> PLL2650XX_MDIV_SHIFT) & PLL2650XX_MDIV_MASK;
	pdiv = (pll_con0 >> PLL2650XX_PDIV_SHIFT) & PLL2650XX_PDIV_MASK;
	sdiv = (pll_con0 >> PLL2650XX_SDIV_SHIFT) & PLL2650XX_SDIV_MASK;
	kdiv = (s16)(pll_con2 & PLL2650XX_KDIV_MASK);

	fvco *= (mdiv << 16) + kdiv;
	do_div(fvco, (pdiv << sdiv));
	fvco >>= 16;

	return (unsigned long)fvco;
}

static int samsung_pll2650xx_set_rate(struct clk_hw *hw, unsigned long drate,
					unsigned long parent_rate)
{
	struct samsung_clk_pll *pll = to_clk_pll(hw);
	u32 tmp, pll_con0, pll_con2;
	const struct samsung_pll_rate_table *rate;

	rate = samsung_get_pll_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
			drate, __clk_get_name(hw->clk));
		return -EINVAL;
	}

	pll_con0 = __raw_readl(pll->con_reg);
	pll_con2 = __raw_readl(pll->con_reg + 8);

	 /* Change PLL PMS values */
	pll_con0 &= ~(PLL2650XX_MDIV_MASK << PLL2650XX_MDIV_SHIFT |
			PLL2650XX_PDIV_MASK << PLL2650XX_PDIV_SHIFT |
			PLL2650XX_SDIV_MASK << PLL2650XX_SDIV_SHIFT);
	pll_con0 |= rate->mdiv << PLL2650XX_MDIV_SHIFT;
	pll_con0 |= rate->pdiv << PLL2650XX_PDIV_SHIFT;
	pll_con0 |= rate->sdiv << PLL2650XX_SDIV_SHIFT;
	pll_con0 |= 1 << PLL2650XX_PLL_ENABLE_SHIFT;
	pll_con0 |= 1 << PLL2650XX_PLL_FOUTMASK_SHIFT;

	pll_con2 &= ~(PLL2650XX_KDIV_MASK << PLL2650XX_KDIV_SHIFT);
	pll_con2 |= ((~(rate->kdiv) + 1) & PLL2650XX_KDIV_MASK)
			<< PLL2650XX_KDIV_SHIFT;

	/* Set PLL lock time. */
	__raw_writel(PLL2650XX_LOCK_FACTOR * rate->pdiv, pll->lock_reg);

	__raw_writel(pll_con0, pll->con_reg);
	__raw_writel(pll_con2, pll->con_reg + 8);

	do {
		tmp = __raw_readl(pll->con_reg);
	} while (!(tmp & (0x1 << PLL2650XX_PLL_LOCKTIME_SHIFT)));

	return 0;
}

static const struct clk_ops samsung_pll2650xx_clk_ops = {
	.recalc_rate = samsung_pll2650xx_recalc_rate,
	.set_rate = samsung_pll2650xx_set_rate,
	.round_rate = samsung_pll_round_rate,
};

static const struct clk_ops samsung_pll2650xx_clk_min_ops = {
	.recalc_rate = samsung_pll2650xx_recalc_rate,
};

static void __init _samsung_clk_register_pll(struct samsung_clk_provider *ctx,
				const struct samsung_pll_clock *pll_clk,
				void __iomem *base)
{
	struct samsung_clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	int ret, len;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll) {
		pr_err("%s: could not allocate pll clk %s\n",
			__func__, pll_clk->name);
		return;
	}

	init.name = pll_clk->name;
	init.flags = pll_clk->flags;
	init.parent_names = &pll_clk->parent_name;
	init.num_parents = 1;

	if (pll_clk->rate_table) {
		/* find count of rates in rate_table */
		for (len = 0; pll_clk->rate_table[len].rate != 0; )
			len++;

		pll->rate_count = len;
		pll->rate_table = kmemdup(pll_clk->rate_table,
					pll->rate_count *
					sizeof(struct samsung_pll_rate_table),
					GFP_KERNEL);
		WARN(!pll->rate_table,
			"%s: could not allocate rate table for %s\n",
			__func__, pll_clk->name);
	}

	switch (pll_clk->type) {
	case pll_2126:
		init.ops = &samsung_pll2126_clk_ops;
		break;
	case pll_3000:
		init.ops = &samsung_pll3000_clk_ops;
		break;
	/* clk_ops for 35xx and 2550 are similar */
	case pll_35xx:
	case pll_2550:
	case pll_1450x:
	case pll_1451x:
	case pll_1452x:
		if (!pll->rate_table)
			init.ops = &samsung_pll35xx_clk_min_ops;
		else
			init.ops = &samsung_pll35xx_clk_ops;
		break;
	case pll_4500:
		init.ops = &samsung_pll45xx_clk_min_ops;
		break;
	case pll_4502:
	case pll_4508:
		if (!pll->rate_table)
			init.ops = &samsung_pll45xx_clk_min_ops;
		else
			init.ops = &samsung_pll45xx_clk_ops;
		break;
	/* clk_ops for 36xx and 2650 are similar */
	case pll_36xx:
	case pll_2650:
		if (!pll->rate_table)
			init.ops = &samsung_pll36xx_clk_min_ops;
		else
			init.ops = &samsung_pll36xx_clk_ops;
		break;
	case pll_6552:
	case pll_6552_s3c2416:
		init.ops = &samsung_pll6552_clk_ops;
		break;
	case pll_6553:
		init.ops = &samsung_pll6553_clk_ops;
		break;
	case pll_4600:
	case pll_4650:
	case pll_4650c:
	case pll_1460x:
		if (!pll->rate_table)
			init.ops = &samsung_pll46xx_clk_min_ops;
		else
			init.ops = &samsung_pll46xx_clk_ops;
		break;
	case pll_s3c2410_mpll:
		if (!pll->rate_table)
			init.ops = &samsung_s3c2410_mpll_clk_min_ops;
		else
			init.ops = &samsung_s3c2410_mpll_clk_ops;
		break;
	case pll_s3c2410_upll:
		if (!pll->rate_table)
			init.ops = &samsung_s3c2410_upll_clk_min_ops;
		else
			init.ops = &samsung_s3c2410_upll_clk_ops;
		break;
	case pll_s3c2440_mpll:
		if (!pll->rate_table)
			init.ops = &samsung_s3c2440_mpll_clk_min_ops;
		else
			init.ops = &samsung_s3c2440_mpll_clk_ops;
		break;
	case pll_2550xx:
		if (!pll->rate_table)
			init.ops = &samsung_pll2550xx_clk_min_ops;
		else
			init.ops = &samsung_pll2550xx_clk_ops;
		break;
	case pll_2650xx:
		if (!pll->rate_table)
			init.ops = &samsung_pll2650xx_clk_min_ops;
		else
			init.ops = &samsung_pll2650xx_clk_ops;
		break;
	default:
		pr_warn("%s: Unknown pll type for pll clk %s\n",
			__func__, pll_clk->name);
	}

	pll->hw.init = &init;
	pll->type = pll_clk->type;
	pll->lock_reg = base + pll_clk->lock_offset;
	pll->con_reg = base + pll_clk->con_offset;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll clock %s : %ld\n",
			__func__, pll_clk->name, PTR_ERR(clk));
		kfree(pll);
		return;
	}

	samsung_clk_add_lookup(ctx, clk, pll_clk->id);

	if (!pll_clk->alias)
		return;

	ret = clk_register_clkdev(clk, pll_clk->alias, pll_clk->dev_name);
	if (ret)
		pr_err("%s: failed to register lookup for %s : %d",
			__func__, pll_clk->name, ret);
}

void __init samsung_clk_register_pll(struct samsung_clk_provider *ctx,
			const struct samsung_pll_clock *pll_list,
			unsigned int nr_pll, void __iomem *base)
{
	int cnt;

	for (cnt = 0; cnt < nr_pll; cnt++)
		_samsung_clk_register_pll(ctx, &pll_list[cnt], base);
}
