// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/err.h>

#include "clk.h"

/**
 * pll v1
 *
 * @clk_hw	clock source
 * @parent	the parent clock name
 * @base	base address of pll registers
 *
 * PLL clock version 1, found on i.MX1/21/25/27/31/35
 */

#define MFN_BITS	(10)
#define MFN_SIGN	(BIT(MFN_BITS - 1))
#define MFN_MASK	(MFN_SIGN - 1)

struct clk_pllv1 {
	struct clk_hw	hw;
	void __iomem	*base;
	enum imx_pllv1_type type;
};

#define to_clk_pllv1(clk) (container_of(clk, struct clk_pllv1, clk))

static inline bool is_imx1_pllv1(struct clk_pllv1 *pll)
{
	return pll->type == IMX_PLLV1_IMX1;
}

static inline bool is_imx21_pllv1(struct clk_pllv1 *pll)
{
	return pll->type == IMX_PLLV1_IMX21;
}

static inline bool is_imx27_pllv1(struct clk_pllv1 *pll)
{
	return pll->type == IMX_PLLV1_IMX27;
}

static inline bool mfn_is_negative(struct clk_pllv1 *pll, unsigned int mfn)
{
	return !is_imx1_pllv1(pll) && !is_imx21_pllv1(pll) && (mfn & MFN_SIGN);
}

static unsigned long clk_pllv1_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_pllv1 *pll = to_clk_pllv1(hw);
	unsigned long long ull;
	int mfn_abs;
	unsigned int mfi, mfn, mfd, pd;
	u32 reg;
	unsigned long rate;

	reg = readl(pll->base);

	/*
	 * Get the resulting clock rate from a PLL register value and the input
	 * frequency. PLLs with this register layout can be found on i.MX1,
	 * i.MX21, i.MX27 and i,MX31
	 *
	 *                  mfi + mfn / (mfd + 1)
	 *  f = 2 * f_ref * --------------------
	 *                        pd + 1
	 */

	mfi = (reg >> 10) & 0xf;
	mfn = reg & 0x3ff;
	mfd = (reg >> 16) & 0x3ff;
	pd =  (reg >> 26) & 0xf;

	mfi = mfi <= 5 ? 5 : mfi;

	mfn_abs = mfn;

	/*
	 * On all i.MXs except i.MX1 and i.MX21 mfn is a 10bit
	 * 2's complements number.
	 * On i.MX27 the bit 9 is the sign bit.
	 */
	if (mfn_is_negative(pll, mfn)) {
		if (is_imx27_pllv1(pll))
			mfn_abs = mfn & MFN_MASK;
		else
			mfn_abs = BIT(MFN_BITS) - mfn;
	}

	rate = parent_rate * 2;
	rate /= pd + 1;

	ull = (unsigned long long)rate * mfn_abs;

	do_div(ull, mfd + 1);

	if (mfn_is_negative(pll, mfn))
		ull = (rate * mfi) - ull;
	else
		ull = (rate * mfi) + ull;

	return ull;
}

static const struct clk_ops clk_pllv1_ops = {
	.recalc_rate = clk_pllv1_recalc_rate,
};

struct clk *imx_clk_pllv1(enum imx_pllv1_type type, const char *name,
		const char *parent, void __iomem *base)
{
	struct clk_pllv1 *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;
	pll->type = type;

	init.name = name;
	init.ops = &clk_pllv1_ops;
	init.flags = 0;
	init.parent_names = &parent;
	init.num_parents = 1;

	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
