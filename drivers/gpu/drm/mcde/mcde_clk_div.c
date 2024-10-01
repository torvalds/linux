// SPDX-License-Identifier: GPL-2.0
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>

#include "mcde_drm.h"
#include "mcde_display_regs.h"

/* The MCDE internal clock dividers for FIFO A and B */
struct mcde_clk_div {
	struct clk_hw hw;
	struct mcde *mcde;
	u32 cr;
	u32 cr_div;
};

static int mcde_clk_div_enable(struct clk_hw *hw)
{
	struct mcde_clk_div *cdiv = container_of(hw, struct mcde_clk_div, hw);
	struct mcde *mcde = cdiv->mcde;
	u32 val;

	spin_lock(&mcde->fifo_crx1_lock);
	val = readl(mcde->regs + cdiv->cr);
	/*
	 * Select the PLL72 (LCD) clock as parent
	 * FIXME: implement other parents.
	 */
	val &= ~MCDE_CRX1_CLKSEL_MASK;
	val |= MCDE_CRX1_CLKSEL_CLKPLL72 << MCDE_CRX1_CLKSEL_SHIFT;
	/* Internal clock */
	val |= MCDE_CRA1_CLKTYPE_TVXCLKSEL1;

	/* Clear then set the divider */
	val &= ~(MCDE_CRX1_BCD | MCDE_CRX1_PCD_MASK);
	val |= cdiv->cr_div;

	writel(val, mcde->regs + cdiv->cr);
	spin_unlock(&mcde->fifo_crx1_lock);

	return 0;
}

static int mcde_clk_div_choose_div(struct clk_hw *hw, unsigned long rate,
				   unsigned long *prate, bool set_parent)
{
	int best_div = 1, div;
	struct clk_hw *parent = clk_hw_get_parent(hw);
	unsigned long best_prate = 0;
	unsigned long best_diff = ~0ul;
	int max_div = (1 << MCDE_CRX1_PCD_BITS) - 1;

	for (div = 1; div < max_div; div++) {
		unsigned long this_prate, div_rate, diff;

		if (set_parent)
			this_prate = clk_hw_round_rate(parent, rate * div);
		else
			this_prate = *prate;
		div_rate = DIV_ROUND_UP_ULL(this_prate, div);
		diff = abs(rate - div_rate);

		if (diff < best_diff) {
			best_div = div;
			best_diff = diff;
			best_prate = this_prate;
		}
	}

	*prate = best_prate;
	return best_div;
}

static long mcde_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	int div = mcde_clk_div_choose_div(hw, rate, prate, true);

	return DIV_ROUND_UP_ULL(*prate, div);
}

static unsigned long mcde_clk_div_recalc_rate(struct clk_hw *hw,
					       unsigned long prate)
{
	struct mcde_clk_div *cdiv = container_of(hw, struct mcde_clk_div, hw);
	struct mcde *mcde = cdiv->mcde;
	u32 cr;
	int div;

	/*
	 * If the MCDE is not powered we can't access registers.
	 * It will come up with 0 in the divider register bits, which
	 * means "divide by 2".
	 */
	if (!regulator_is_enabled(mcde->epod))
		return DIV_ROUND_UP_ULL(prate, 2);

	cr = readl(mcde->regs + cdiv->cr);
	if (cr & MCDE_CRX1_BCD)
		return prate;

	/* 0 in the PCD means "divide by 2", 1 means "divide by 3" etc */
	div = cr & MCDE_CRX1_PCD_MASK;
	div += 2;

	return DIV_ROUND_UP_ULL(prate, div);
}

static int mcde_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long prate)
{
	struct mcde_clk_div *cdiv = container_of(hw, struct mcde_clk_div, hw);
	int div = mcde_clk_div_choose_div(hw, rate, &prate, false);
	u32 cr = 0;

	/*
	 * We cache the CR bits to set the divide in the state so that
	 * we can call this before we can even write to the hardware.
	 */
	if (div == 1) {
		/* Bypass clock divider */
		cr |= MCDE_CRX1_BCD;
	} else {
		div -= 2;
		cr |= div & MCDE_CRX1_PCD_MASK;
	}
	cdiv->cr_div = cr;

	return 0;
}

static const struct clk_ops mcde_clk_div_ops = {
	.enable = mcde_clk_div_enable,
	.recalc_rate = mcde_clk_div_recalc_rate,
	.round_rate = mcde_clk_div_round_rate,
	.set_rate = mcde_clk_div_set_rate,
};

int mcde_init_clock_divider(struct mcde *mcde)
{
	struct device *dev = mcde->dev;
	struct mcde_clk_div *fifoa;
	struct mcde_clk_div *fifob;
	const char *parent_name;
	struct clk_init_data fifoa_init = {
		.name = "fifoa",
		.ops = &mcde_clk_div_ops,
		.parent_names = &parent_name,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	};
	struct clk_init_data fifob_init = {
		.name = "fifob",
		.ops = &mcde_clk_div_ops,
		.parent_names = &parent_name,
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	};
	int ret;

	spin_lock_init(&mcde->fifo_crx1_lock);
	parent_name = __clk_get_name(mcde->lcd_clk);

	/* Allocate 2 clocks */
	fifoa = devm_kzalloc(dev, sizeof(*fifoa), GFP_KERNEL);
	if (!fifoa)
		return -ENOMEM;
	fifob = devm_kzalloc(dev, sizeof(*fifob), GFP_KERNEL);
	if (!fifob)
		return -ENOMEM;

	fifoa->mcde = mcde;
	fifoa->cr = MCDE_CRA1;
	fifoa->hw.init = &fifoa_init;
	ret = devm_clk_hw_register(dev, &fifoa->hw);
	if (ret) {
		dev_err(dev, "error registering FIFO A clock divider\n");
		return ret;
	}
	mcde->fifoa_clk = fifoa->hw.clk;

	fifob->mcde = mcde;
	fifob->cr = MCDE_CRB1;
	fifob->hw.init = &fifob_init;
	ret = devm_clk_hw_register(dev, &fifob->hw);
	if (ret) {
		dev_err(dev, "error registering FIFO B clock divider\n");
		return ret;
	}
	mcde->fifob_clk = fifob->hw.clk;

	return 0;
}
