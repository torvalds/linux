// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "clk-pll.h"

#define MHZ			(1000 * 1000)

#define REG_CON0		0
#define REG_CON1		4

#define CON0_BASE_EN		BIT(0)
#define CON0_PWR_ON		BIT(0)
#define CON0_ISO_EN		BIT(1)
#define PCW_CHG_MASK		BIT(31)

#define AUDPLL_TUNER_EN		BIT(31)

/* default 7 bits integer, can be overridden with pcwibits. */
#define INTEGER_BITS		7

int mtk_pll_is_prepared(struct clk_hw *hw)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);

	return (readl(pll->en_addr) & BIT(pll->data->pll_en_bit)) != 0;
}

static unsigned long __mtk_pll_recalc_rate(struct mtk_clk_pll *pll, u32 fin,
		u32 pcw, int postdiv)
{
	int pcwbits = pll->data->pcwbits;
	int pcwfbits = 0;
	int ibits;
	u64 vco;
	u8 c = 0;

	/* The fractional part of the PLL divider. */
	ibits = pll->data->pcwibits ? pll->data->pcwibits : INTEGER_BITS;
	if (pcwbits > ibits)
		pcwfbits = pcwbits - ibits;

	vco = (u64)fin * pcw;

	if (pcwfbits && (vco & GENMASK(pcwfbits - 1, 0)))
		c = 1;

	vco >>= pcwfbits;

	if (c)
		vco++;

	return ((unsigned long)vco + postdiv - 1) / postdiv;
}

static void __mtk_pll_tuner_enable(struct mtk_clk_pll *pll)
{
	u32 r;

	if (pll->tuner_en_addr) {
		r = readl(pll->tuner_en_addr) | BIT(pll->data->tuner_en_bit);
		writel(r, pll->tuner_en_addr);
	} else if (pll->tuner_addr) {
		r = readl(pll->tuner_addr) | AUDPLL_TUNER_EN;
		writel(r, pll->tuner_addr);
	}
}

static void __mtk_pll_tuner_disable(struct mtk_clk_pll *pll)
{
	u32 r;

	if (pll->tuner_en_addr) {
		r = readl(pll->tuner_en_addr) & ~BIT(pll->data->tuner_en_bit);
		writel(r, pll->tuner_en_addr);
	} else if (pll->tuner_addr) {
		r = readl(pll->tuner_addr) & ~AUDPLL_TUNER_EN;
		writel(r, pll->tuner_addr);
	}
}

static void mtk_pll_set_rate_regs(struct mtk_clk_pll *pll, u32 pcw,
		int postdiv)
{
	u32 chg, val;

	/* disable tuner */
	__mtk_pll_tuner_disable(pll);

	/* set postdiv */
	val = readl(pll->pd_addr);
	val &= ~(POSTDIV_MASK << pll->data->pd_shift);
	val |= (ffs(postdiv) - 1) << pll->data->pd_shift;

	/* postdiv and pcw need to set at the same time if on same register */
	if (pll->pd_addr != pll->pcw_addr) {
		writel(val, pll->pd_addr);
		val = readl(pll->pcw_addr);
	}

	/* set pcw */
	val &= ~GENMASK(pll->data->pcw_shift + pll->data->pcwbits - 1,
			pll->data->pcw_shift);
	val |= pcw << pll->data->pcw_shift;
	writel(val, pll->pcw_addr);
	chg = readl(pll->pcw_chg_addr) | PCW_CHG_MASK;
	writel(chg, pll->pcw_chg_addr);
	if (pll->tuner_addr)
		writel(val + 1, pll->tuner_addr);

	/* restore tuner_en */
	__mtk_pll_tuner_enable(pll);

	udelay(20);
}

/*
 * mtk_pll_calc_values - calculate good values for a given input frequency.
 * @pll:	The pll
 * @pcw:	The pcw value (output)
 * @postdiv:	The post divider (output)
 * @freq:	The desired target frequency
 * @fin:	The input frequency
 *
 */
void mtk_pll_calc_values(struct mtk_clk_pll *pll, u32 *pcw, u32 *postdiv,
			 u32 freq, u32 fin)
{
	unsigned long fmin = pll->data->fmin ? pll->data->fmin : (1000 * MHZ);
	const struct mtk_pll_div_table *div_table = pll->data->div_table;
	u64 _pcw;
	int ibits;
	u32 val;

	if (freq > pll->data->fmax)
		freq = pll->data->fmax;

	if (div_table) {
		if (freq > div_table[0].freq)
			freq = div_table[0].freq;

		for (val = 0; div_table[val + 1].freq != 0; val++) {
			if (freq > div_table[val + 1].freq)
				break;
		}
		*postdiv = 1 << val;
	} else {
		for (val = 0; val < 5; val++) {
			*postdiv = 1 << val;
			if ((u64)freq * *postdiv >= fmin)
				break;
		}
	}

	/* _pcw = freq * postdiv / fin * 2^pcwfbits */
	ibits = pll->data->pcwibits ? pll->data->pcwibits : INTEGER_BITS;
	_pcw = ((u64)freq << val) << (pll->data->pcwbits - ibits);
	do_div(_pcw, fin);

	*pcw = (u32)_pcw;
}

int mtk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
		     unsigned long parent_rate)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	u32 pcw = 0;
	u32 postdiv;

	mtk_pll_calc_values(pll, &pcw, &postdiv, rate, parent_rate);
	mtk_pll_set_rate_regs(pll, pcw, postdiv);

	return 0;
}

unsigned long mtk_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	u32 postdiv;
	u32 pcw;

	postdiv = (readl(pll->pd_addr) >> pll->data->pd_shift) & POSTDIV_MASK;
	postdiv = 1 << postdiv;

	pcw = readl(pll->pcw_addr) >> pll->data->pcw_shift;
	pcw &= GENMASK(pll->data->pcwbits - 1, 0);

	return __mtk_pll_recalc_rate(pll, parent_rate, pcw, postdiv);
}

long mtk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	u32 pcw = 0;
	int postdiv;

	mtk_pll_calc_values(pll, &pcw, &postdiv, rate, *prate);

	return __mtk_pll_recalc_rate(pll, *prate, pcw, postdiv);
}

int mtk_pll_prepare(struct clk_hw *hw)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	u32 r;

	r = readl(pll->pwr_addr) | CON0_PWR_ON;
	writel(r, pll->pwr_addr);
	udelay(1);

	r = readl(pll->pwr_addr) & ~CON0_ISO_EN;
	writel(r, pll->pwr_addr);
	udelay(1);

	r = readl(pll->en_addr) | BIT(pll->data->pll_en_bit);
	writel(r, pll->en_addr);

	if (pll->data->en_mask) {
		r = readl(pll->base_addr + REG_CON0) | pll->data->en_mask;
		writel(r, pll->base_addr + REG_CON0);
	}

	__mtk_pll_tuner_enable(pll);

	udelay(20);

	if (pll->data->flags & HAVE_RST_BAR) {
		r = readl(pll->base_addr + REG_CON0);
		r |= pll->data->rst_bar_mask;
		writel(r, pll->base_addr + REG_CON0);
	}

	return 0;
}

void mtk_pll_unprepare(struct clk_hw *hw)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);
	u32 r;

	if (pll->data->flags & HAVE_RST_BAR) {
		r = readl(pll->base_addr + REG_CON0);
		r &= ~pll->data->rst_bar_mask;
		writel(r, pll->base_addr + REG_CON0);
	}

	__mtk_pll_tuner_disable(pll);

	if (pll->data->en_mask) {
		r = readl(pll->base_addr + REG_CON0) & ~pll->data->en_mask;
		writel(r, pll->base_addr + REG_CON0);
	}

	r = readl(pll->en_addr) & ~BIT(pll->data->pll_en_bit);
	writel(r, pll->en_addr);

	r = readl(pll->pwr_addr) | CON0_ISO_EN;
	writel(r, pll->pwr_addr);

	r = readl(pll->pwr_addr) & ~CON0_PWR_ON;
	writel(r, pll->pwr_addr);
}

const struct clk_ops mtk_pll_ops = {
	.is_prepared	= mtk_pll_is_prepared,
	.prepare	= mtk_pll_prepare,
	.unprepare	= mtk_pll_unprepare,
	.recalc_rate	= mtk_pll_recalc_rate,
	.round_rate	= mtk_pll_round_rate,
	.set_rate	= mtk_pll_set_rate,
};

struct clk_hw *mtk_clk_register_pll_ops(struct mtk_clk_pll *pll,
					const struct mtk_pll_data *data,
					void __iomem *base,
					const struct clk_ops *pll_ops)
{
	struct clk_init_data init = {};
	int ret;
	const char *parent_name = "clk26m";

	pll->base_addr = base + data->reg;
	pll->pwr_addr = base + data->pwr_reg;
	pll->pd_addr = base + data->pd_reg;
	pll->pcw_addr = base + data->pcw_reg;
	if (data->pcw_chg_reg)
		pll->pcw_chg_addr = base + data->pcw_chg_reg;
	else
		pll->pcw_chg_addr = pll->base_addr + REG_CON1;
	if (data->tuner_reg)
		pll->tuner_addr = base + data->tuner_reg;
	if (data->tuner_en_reg || data->tuner_en_bit)
		pll->tuner_en_addr = base + data->tuner_en_reg;
	if (data->en_reg)
		pll->en_addr = base + data->en_reg;
	else
		pll->en_addr = pll->base_addr + REG_CON0;
	pll->hw.init = &init;
	pll->data = data;

	init.name = data->name;
	init.flags = (data->flags & PLL_AO) ? CLK_IS_CRITICAL : 0;
	init.ops = pll_ops;
	if (data->parent_name)
		init.parent_names = &data->parent_name;
	else
		init.parent_names = &parent_name;
	init.num_parents = 1;

	ret = clk_hw_register(NULL, &pll->hw);

	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	return &pll->hw;
}

struct clk_hw *mtk_clk_register_pll(const struct mtk_pll_data *data,
				    void __iomem *base)
{
	struct mtk_clk_pll *pll;
	struct clk_hw *hw;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	hw = mtk_clk_register_pll_ops(pll, data, base, &mtk_pll_ops);

	return hw;
}

void mtk_clk_unregister_pll(struct clk_hw *hw)
{
	struct mtk_clk_pll *pll;

	if (!hw)
		return;

	pll = to_mtk_clk_pll(hw);

	clk_hw_unregister(hw);
	kfree(pll);
}

int mtk_clk_register_plls(struct device_node *node,
			  const struct mtk_pll_data *plls, int num_plls,
			  struct clk_hw_onecell_data *clk_data)
{
	void __iomem *base;
	int i;
	struct clk_hw *hw;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < num_plls; i++) {
		const struct mtk_pll_data *pll = &plls[i];

		if (!IS_ERR_OR_NULL(clk_data->hws[pll->id])) {
			pr_warn("%pOF: Trying to register duplicate clock ID: %d\n",
				node, pll->id);
			continue;
		}

		hw = mtk_clk_register_pll(pll, base);

		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %pe\n", pll->name,
			       hw);
			goto err;
		}

		clk_data->hws[pll->id] = hw;
	}

	return 0;

err:
	while (--i >= 0) {
		const struct mtk_pll_data *pll = &plls[i];

		mtk_clk_unregister_pll(clk_data->hws[pll->id]);
		clk_data->hws[pll->id] = ERR_PTR(-ENOENT);
	}

	iounmap(base);

	return PTR_ERR(hw);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_plls);

__iomem void *mtk_clk_pll_get_base(struct clk_hw *hw,
				   const struct mtk_pll_data *data)
{
	struct mtk_clk_pll *pll = to_mtk_clk_pll(hw);

	return pll->base_addr - data->reg;
}

void mtk_clk_unregister_plls(const struct mtk_pll_data *plls, int num_plls,
			     struct clk_hw_onecell_data *clk_data)
{
	__iomem void *base = NULL;
	int i;

	if (!clk_data)
		return;

	for (i = num_plls; i > 0; i--) {
		const struct mtk_pll_data *pll = &plls[i - 1];

		if (IS_ERR_OR_NULL(clk_data->hws[pll->id]))
			continue;

		/*
		 * This is quite ugly but unfortunately the clks don't have
		 * any device tied to them, so there's no place to store the
		 * pointer to the I/O region base address. We have to fetch
		 * it from one of the registered clks.
		 */
		base = mtk_clk_pll_get_base(clk_data->hws[pll->id], pll);

		mtk_clk_unregister_pll(clk_data->hws[pll->id]);
		clk_data->hws[pll->id] = ERR_PTR(-ENOENT);
	}

	iounmap(base);
}
EXPORT_SYMBOL_GPL(mtk_clk_unregister_plls);

MODULE_LICENSE("GPL");
