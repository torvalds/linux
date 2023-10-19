// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MMP PLL clock rate calculation
 *
 * Copyright (C) 2020 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "clk.h"

#define to_clk_mmp_pll(hw)	container_of(hw, struct mmp_clk_pll, hw)

struct mmp_clk_pll {
	struct clk_hw hw;
	unsigned long default_rate;
	void __iomem *enable_reg;
	u32 enable;
	void __iomem *reg;
	u8 shift;

	unsigned long input_rate;
	void __iomem *postdiv_reg;
	u8 postdiv_shift;
};

static int mmp_clk_pll_is_enabled(struct clk_hw *hw)
{
	struct mmp_clk_pll *pll = to_clk_mmp_pll(hw);
	u32 val;

	val = readl_relaxed(pll->enable_reg);
	if ((val & pll->enable) == pll->enable)
		return 1;

	/* Some PLLs, if not software controlled, output default clock. */
	if (pll->default_rate > 0)
		return 1;

	return 0;
}

static unsigned long mmp_clk_pll_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct mmp_clk_pll *pll = to_clk_mmp_pll(hw);
	u32 fbdiv, refdiv, postdiv;
	u64 rate;
	u32 val;

	val = readl_relaxed(pll->enable_reg);
	if ((val & pll->enable) != pll->enable)
		return pll->default_rate;

	if (pll->reg) {
		val = readl_relaxed(pll->reg);
		fbdiv = (val >> pll->shift) & 0x1ff;
		refdiv = (val >> (pll->shift + 9)) & 0x1f;
	} else {
		fbdiv = 2;
		refdiv = 1;
	}

	if (pll->postdiv_reg) {
		/* MMP3 clock rate calculation */
		static const u8 postdivs[] = {2, 3, 4, 5, 6, 8, 10, 12, 16};

		val = readl_relaxed(pll->postdiv_reg);
		postdiv = (val >> pll->postdiv_shift) & 0x7;

		rate = pll->input_rate;
		rate *= 2 * fbdiv;
		do_div(rate, refdiv);
		do_div(rate, postdivs[postdiv]);
	} else {
		/* MMP2 clock rate calculation */
		if (refdiv == 3) {
			rate = 19200000;
		} else if (refdiv == 4) {
			rate = 26000000;
		} else {
			pr_err("bad refdiv: %d (0x%08x)\n", refdiv, val);
			return 0;
		}

		rate *= fbdiv + 2;
		do_div(rate, refdiv + 2);
	}

	return (unsigned long)rate;
}

static const struct clk_ops mmp_clk_pll_ops = {
	.is_enabled = mmp_clk_pll_is_enabled,
	.recalc_rate = mmp_clk_pll_recalc_rate,
};

static struct clk *mmp_clk_register_pll(char *name,
			unsigned long default_rate,
			void __iomem *enable_reg, u32 enable,
			void __iomem *reg, u8 shift,
			unsigned long input_rate,
			void __iomem *postdiv_reg, u8 postdiv_shift)
{
	struct mmp_clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &mmp_clk_pll_ops;
	init.flags = 0;
	init.parent_names = NULL;
	init.num_parents = 0;

	pll->default_rate = default_rate;
	pll->enable_reg = enable_reg;
	pll->enable = enable;
	pll->reg = reg;
	pll->shift = shift;

	pll->input_rate = input_rate;
	pll->postdiv_reg = postdiv_reg;
	pll->postdiv_shift = postdiv_shift;

	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);

	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

void mmp_register_pll_clks(struct mmp_clk_unit *unit,
			struct mmp_param_pll_clk *clks,
			void __iomem *base, int size)
{
	struct clk *clk;
	int i;

	for (i = 0; i < size; i++) {
		void __iomem *reg = NULL;

		if (clks[i].offset)
			reg = base + clks[i].offset;

		clk = mmp_clk_register_pll(clks[i].name,
					clks[i].default_rate,
					base + clks[i].enable_offset,
					clks[i].enable,
					reg, clks[i].shift,
					clks[i].input_rate,
					base + clks[i].postdiv_offset,
					clks[i].postdiv_shift);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
		if (clks[i].id)
			unit->clk_table[clks[i].id] = clk;
	}
}
