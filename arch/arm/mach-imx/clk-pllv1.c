#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/err.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"

/**
 * pll v1
 *
 * @clk_hw	clock source
 * @parent	the parent clock name
 * @base	base address of pll registers
 *
 * PLL clock version 1, found on i.MX1/21/25/27/31/35
 */
struct clk_pllv1 {
	struct clk_hw	hw;
	void __iomem	*base;
};

#define to_clk_pllv1(clk) (container_of(clk, struct clk_pllv1, clk))

static unsigned long clk_pllv1_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_pllv1 *pll = to_clk_pllv1(hw);
	long long ll;
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
	 * 2's complements number
	 */
	if (!cpu_is_mx1() && !cpu_is_mx21() && mfn >= 0x200)
		mfn_abs = 0x400 - mfn;

	rate = parent_rate * 2;
	rate /= pd + 1;

	ll = (unsigned long long)rate * mfn_abs;

	do_div(ll, mfd + 1);

	if (!cpu_is_mx1() && !cpu_is_mx21() && mfn >= 0x200)
		ll = -ll;

	ll = (rate * mfi) + ll;

	return ll;
}

struct clk_ops clk_pllv1_ops = {
	.recalc_rate = clk_pllv1_recalc_rate,
};

struct clk *imx_clk_pllv1(const char *name, const char *parent,
		void __iomem *base)
{
	struct clk_pllv1 *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kmalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;

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
