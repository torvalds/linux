// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>

#include <asm/div64.h>

#include "clk.h"

#define to_clk_pllv2(clk) (container_of(clk, struct clk_pllv2, clk))

/* PLL Register Offsets */
#define MXC_PLL_DP_CTL			0x00
#define MXC_PLL_DP_CONFIG		0x04
#define MXC_PLL_DP_OP			0x08
#define MXC_PLL_DP_MFD			0x0C
#define MXC_PLL_DP_MFN			0x10
#define MXC_PLL_DP_MFNMINUS		0x14
#define MXC_PLL_DP_MFNPLUS		0x18
#define MXC_PLL_DP_HFS_OP		0x1C
#define MXC_PLL_DP_HFS_MFD		0x20
#define MXC_PLL_DP_HFS_MFN		0x24
#define MXC_PLL_DP_MFN_TOGC		0x28
#define MXC_PLL_DP_DESTAT		0x2c

/* PLL Register Bit definitions */
#define MXC_PLL_DP_CTL_MUL_CTRL		0x2000
#define MXC_PLL_DP_CTL_DPDCK0_2_EN	0x1000
#define MXC_PLL_DP_CTL_DPDCK0_2_OFFSET	12
#define MXC_PLL_DP_CTL_ADE		0x800
#define MXC_PLL_DP_CTL_REF_CLK_DIV	0x400
#define MXC_PLL_DP_CTL_REF_CLK_SEL_MASK	(3 << 8)
#define MXC_PLL_DP_CTL_REF_CLK_SEL_OFFSET	8
#define MXC_PLL_DP_CTL_HFSM		0x80
#define MXC_PLL_DP_CTL_PRE		0x40
#define MXC_PLL_DP_CTL_UPEN		0x20
#define MXC_PLL_DP_CTL_RST		0x10
#define MXC_PLL_DP_CTL_RCP		0x8
#define MXC_PLL_DP_CTL_PLM		0x4
#define MXC_PLL_DP_CTL_BRM0		0x2
#define MXC_PLL_DP_CTL_LRF		0x1

#define MXC_PLL_DP_CONFIG_BIST		0x8
#define MXC_PLL_DP_CONFIG_SJC_CE	0x4
#define MXC_PLL_DP_CONFIG_AREN		0x2
#define MXC_PLL_DP_CONFIG_LDREQ		0x1

#define MXC_PLL_DP_OP_MFI_OFFSET	4
#define MXC_PLL_DP_OP_MFI_MASK		(0xF << 4)
#define MXC_PLL_DP_OP_PDF_OFFSET	0
#define MXC_PLL_DP_OP_PDF_MASK		0xF

#define MXC_PLL_DP_MFD_OFFSET		0
#define MXC_PLL_DP_MFD_MASK		0x07FFFFFF

#define MXC_PLL_DP_MFN_OFFSET		0x0
#define MXC_PLL_DP_MFN_MASK		0x07FFFFFF

#define MXC_PLL_DP_MFN_TOGC_TOG_DIS	(1 << 17)
#define MXC_PLL_DP_MFN_TOGC_TOG_EN	(1 << 16)
#define MXC_PLL_DP_MFN_TOGC_CNT_OFFSET	0x0
#define MXC_PLL_DP_MFN_TOGC_CNT_MASK	0xFFFF

#define MXC_PLL_DP_DESTAT_TOG_SEL	(1 << 31)
#define MXC_PLL_DP_DESTAT_MFN		0x07FFFFFF

#define MAX_DPLL_WAIT_TRIES	1000 /* 1000 * udelay(1) = 1ms */

struct clk_pllv2 {
	struct clk_hw	hw;
	void __iomem	*base;
};

static unsigned long __clk_pllv2_recalc_rate(unsigned long parent_rate,
		u32 dp_ctl, u32 dp_op, u32 dp_mfd, u32 dp_mfn)
{
	long mfi, mfn, mfd, pdf, ref_clk;
	unsigned long dbl;
	u64 temp;

	dbl = dp_ctl & MXC_PLL_DP_CTL_DPDCK0_2_EN;

	pdf = dp_op & MXC_PLL_DP_OP_PDF_MASK;
	mfi = (dp_op & MXC_PLL_DP_OP_MFI_MASK) >> MXC_PLL_DP_OP_MFI_OFFSET;
	mfi = (mfi <= 5) ? 5 : mfi;
	mfd = dp_mfd & MXC_PLL_DP_MFD_MASK;
	mfn = dp_mfn & MXC_PLL_DP_MFN_MASK;
	mfn = sign_extend32(mfn, 26);

	ref_clk = 2 * parent_rate;
	if (dbl != 0)
		ref_clk *= 2;

	ref_clk /= (pdf + 1);
	temp = (u64) ref_clk * abs(mfn);
	do_div(temp, mfd + 1);
	if (mfn < 0)
		temp = (ref_clk * mfi) - temp;
	else
		temp = (ref_clk * mfi) + temp;

	return temp;
}

static unsigned long clk_pllv2_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	u32 dp_op, dp_mfd, dp_mfn, dp_ctl;
	void __iomem *pllbase;
	struct clk_pllv2 *pll = to_clk_pllv2(hw);

	pllbase = pll->base;

	dp_ctl = __raw_readl(pllbase + MXC_PLL_DP_CTL);
	dp_op = __raw_readl(pllbase + MXC_PLL_DP_OP);
	dp_mfd = __raw_readl(pllbase + MXC_PLL_DP_MFD);
	dp_mfn = __raw_readl(pllbase + MXC_PLL_DP_MFN);

	return __clk_pllv2_recalc_rate(parent_rate, dp_ctl, dp_op, dp_mfd, dp_mfn);
}

static int __clk_pllv2_set_rate(unsigned long rate, unsigned long parent_rate,
		u32 *dp_op, u32 *dp_mfd, u32 *dp_mfn)
{
	u32 reg;
	long mfi, pdf, mfn, mfd = 999999;
	u64 temp64;
	unsigned long quad_parent_rate;

	quad_parent_rate = 4 * parent_rate;
	pdf = mfi = -1;
	while (++pdf < 16 && mfi < 5)
		mfi = rate * (pdf+1) / quad_parent_rate;
	if (mfi > 15)
		return -EINVAL;
	pdf--;

	temp64 = rate * (pdf + 1) - quad_parent_rate * mfi;
	do_div(temp64, quad_parent_rate / 1000000);
	mfn = (long)temp64;

	reg = mfi << 4 | pdf;

	*dp_op = reg;
	*dp_mfd = mfd;
	*dp_mfn = mfn;

	return 0;
}

static int clk_pllv2_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_pllv2 *pll = to_clk_pllv2(hw);
	void __iomem *pllbase;
	u32 dp_ctl, dp_op, dp_mfd, dp_mfn;
	int ret;

	pllbase = pll->base;


	ret = __clk_pllv2_set_rate(rate, parent_rate, &dp_op, &dp_mfd, &dp_mfn);
	if (ret)
		return ret;

	dp_ctl = __raw_readl(pllbase + MXC_PLL_DP_CTL);
	/* use dpdck0_2 */
	__raw_writel(dp_ctl | 0x1000L, pllbase + MXC_PLL_DP_CTL);

	__raw_writel(dp_op, pllbase + MXC_PLL_DP_OP);
	__raw_writel(dp_mfd, pllbase + MXC_PLL_DP_MFD);
	__raw_writel(dp_mfn, pllbase + MXC_PLL_DP_MFN);

	return 0;
}

static long clk_pllv2_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	u32 dp_op, dp_mfd, dp_mfn;

	__clk_pllv2_set_rate(rate, *prate, &dp_op, &dp_mfd, &dp_mfn);
	return __clk_pllv2_recalc_rate(*prate, MXC_PLL_DP_CTL_DPDCK0_2_EN,
			dp_op, dp_mfd, dp_mfn);
}

static int clk_pllv2_prepare(struct clk_hw *hw)
{
	struct clk_pllv2 *pll = to_clk_pllv2(hw);
	u32 reg;
	void __iomem *pllbase;
	int i = 0;

	pllbase = pll->base;
	reg = __raw_readl(pllbase + MXC_PLL_DP_CTL) | MXC_PLL_DP_CTL_UPEN;
	__raw_writel(reg, pllbase + MXC_PLL_DP_CTL);

	/* Wait for lock */
	do {
		reg = __raw_readl(pllbase + MXC_PLL_DP_CTL);
		if (reg & MXC_PLL_DP_CTL_LRF)
			break;

		udelay(1);
	} while (++i < MAX_DPLL_WAIT_TRIES);

	if (i == MAX_DPLL_WAIT_TRIES) {
		pr_err("MX5: pll locking failed\n");
		return -EINVAL;
	}

	return 0;
}

static void clk_pllv2_unprepare(struct clk_hw *hw)
{
	struct clk_pllv2 *pll = to_clk_pllv2(hw);
	u32 reg;
	void __iomem *pllbase;

	pllbase = pll->base;
	reg = __raw_readl(pllbase + MXC_PLL_DP_CTL) & ~MXC_PLL_DP_CTL_UPEN;
	__raw_writel(reg, pllbase + MXC_PLL_DP_CTL);
}

static const struct clk_ops clk_pllv2_ops = {
	.prepare = clk_pllv2_prepare,
	.unprepare = clk_pllv2_unprepare,
	.recalc_rate = clk_pllv2_recalc_rate,
	.round_rate = clk_pllv2_round_rate,
	.set_rate = clk_pllv2_set_rate,
};

struct clk *imx_clk_pllv2(const char *name, const char *parent,
		void __iomem *base)
{
	struct clk_pllv2 *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->base = base;

	init.name = name;
	init.ops = &clk_pllv2_ops;
	init.flags = 0;
	init.parent_names = &parent;
	init.num_parents = 1;

	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
