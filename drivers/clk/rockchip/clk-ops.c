#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-private.h>
#include <linux/delay.h>
#include <linux/rockchip/common.h>

#include "clk-ops.h"

/* mux_ops */
struct clk_ops_table rk_clk_mux_ops_table[] = {
	{.index = CLKOPS_TABLE_END},
};


/* rate_ops */
#define to_clk_divider(_hw) container_of(_hw, struct clk_divider, hw)
#define div_mask(d)	((1 << ((d)->width)) - 1)

static u32 clk_gcd(u32 numerator, u32 denominator)
{
	u32 a, b;

	if (!numerator || !denominator)
		return 0;
	if (numerator > denominator) {
		a = numerator;
		b = denominator;
	} else {
		a = denominator;
		b = numerator;
	}
	while (b != 0) {
		int r = b;
		b = a % b;
		a = r;
	}

	return a;
}

static int clk_fracdiv_get_config(unsigned long rate_out, unsigned long rate,
		u32 *numerator, u32 *denominator)
{
	u32 gcd_val;
	gcd_val = clk_gcd(rate, rate_out);
	clk_debug("%s: frac_get_seting rate=%lu, parent=%lu, gcd=%d\n",
			__func__, rate_out, rate, gcd_val);

	if (!gcd_val) {
		clk_err("gcd=0, frac div is not be supported\n");
		return -EINVAL;
	}

	*numerator = rate_out / gcd_val;
	*denominator = rate / gcd_val;

	clk_debug("%s: frac_get_seting numerator=%d, denominator=%d, times=%d\n",
			__func__, *numerator, *denominator,
			*denominator / *numerator);

	if (*numerator > 0xffff || *denominator > 0xffff ||
			(*denominator / (*numerator)) < 20) {
		clk_err("can't get a available nume and deno\n");
		return -EINVAL;
	}

	return 0;

}

static int clk_fracdiv_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	u32 numerator, denominator;
	struct clk_divider *div = to_clk_divider(hw);


	if(clk_fracdiv_get_config(rate, parent_rate,
				&numerator, &denominator) == 0) {
		writel(numerator << 16 | denominator, div->reg);
		clk_debug("%s set rate=%lu,is ok\n", hw->clk->name, rate);
	} else {
		clk_err("clk_frac_div name=%s can't get rate=%lu\n",
				hw->clk->name, rate);
		return -EINVAL;
	}

	return 0;
}

static unsigned long clk_fracdiv_recalc(struct clk_hw *hw,
		unsigned long parent_rate)
{
	unsigned long rate;
	u64 rate64;
	struct clk_divider *div = to_clk_divider(hw);
	u32 numerator, denominator, reg_val;

	reg_val = readl(div->reg);
	if (reg_val == 0)
		return parent_rate;

	numerator = reg_val >> 16;
	denominator = reg_val & 0xFFFF;
	rate64 = (u64)parent_rate * numerator;
	do_div(rate64, denominator);
	rate = rate64;
	clk_debug("%s: %s new clock rate is %lu, prate %lu (frac %u/%u)\n",
			__func__, hw->clk->name, rate, parent_rate,
			numerator, denominator);
	return rate;
}

static long clk_fracdiv_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct clk *clk = hw->clk;
	struct clk *parent = clk->parent;
	long rate_out;

	//FIXME: now just simply return rate
	/*
	 *frac_div request a big input rate, and its parent is always a div,
	 *so we set parent->parent->rate as best_parent_rate.
	 */
	rate_out = rate;
	*prate = parent->parent->rate;

	return rate_out;
}

static unsigned long clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static long clk_divider_round_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long *prate)
{
	return clk_divider_ops.round_rate(hw, rate, prate);
}

static int clk_divider_set_rate(struct clk_hw *hw,
		unsigned long rate, unsigned long parent_rate)
{
	return clk_divider_ops.set_rate(hw, rate, parent_rate);
}

static long clk_mux_with_div_determine_rate(struct clk_hw *div_hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	struct clk *clk = div_hw->clk, *parent = NULL, *best_parent = NULL;
	int i, num_parents;
	unsigned long parent_rate = 0, best_prate = 0, best = 0, now = 0;


	parent = __clk_get_parent(clk);
	if(!parent){
		best = __clk_get_rate(clk);
		goto out;
	}

	/* if NO_REPARENT flag set, pass through to current parent */
	if (clk->flags & CLK_SET_RATE_NO_REPARENT) {
		best_prate = __clk_get_rate(parent);
		best = clk_divider_ops.round_rate(div_hw, rate, &best_prate);
		goto out;
	}

	/* find the parent that can provide the fastest rate <= rate */
	num_parents = clk->num_parents;
	for (i = 0; i < num_parents; i++) {
		parent = clk_get_parent_by_index(clk, i);
		if (!parent)
			continue;

		parent_rate = __clk_get_rate(parent);
		now = clk_divider_ops.round_rate(div_hw, rate, &parent_rate);

		if (now <= rate && now > best) {
			best_parent = parent;
			best_prate = parent_rate;
			best = now;
		}
	}

out:
	if(best_prate)
		*best_parent_rate = best_prate;

	if (best_parent)
		*best_parent_p = best_parent;

	clk_debug("clk name = %s, determine rate = %lu, best = %lu\n"
			"\tbest_parent name = %s, best_prate = %lu\n",
			clk->name, rate, best,
			__clk_get_name(*best_parent_p), *best_parent_rate);

	return best;
}

const struct clk_ops clkops_rate_auto_parent = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= clk_divider_round_rate,
	.set_rate	= clk_divider_set_rate,
	.determine_rate = clk_mux_with_div_determine_rate,
};

static long clk_div_round_rate_even(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	int i = 0;
	struct clk_divider *divider =to_clk_divider(hw);
	int max_div = 1 << divider->width;

	for (i = 1; i <= max_div; i++) {
		if (i > 1 && (i % 2 != 0))
			continue;
		if (rate >= (*prate / i))
			return *prate / i;
	}

	return (*prate / max_div);
}

const struct clk_ops clkops_rate_evendiv = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= clk_div_round_rate_even,
	.set_rate	= clk_divider_set_rate,
};

static long clk_mux_with_evendiv_determine_rate(struct clk_hw *div_hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	struct clk *clk = div_hw->clk, *parent = NULL, *best_parent = NULL;
	int i, num_parents;
	unsigned long parent_rate = 0, best_prate = 0, best = 0, now = 0;


	parent = __clk_get_parent(clk);
	if(!parent){
		best = __clk_get_rate(clk);
		goto out;
	}

	/* if NO_REPARENT flag set, pass through to current parent */
	if (clk->flags & CLK_SET_RATE_NO_REPARENT) {
		best_prate = __clk_get_rate(parent);
		best = clk_div_round_rate_even(div_hw, rate, &best_prate);
		goto out;
	}

	/* find the parent that can provide the fastest rate <= rate */
	num_parents = clk->num_parents;
	for (i = 0; i < num_parents; i++) {
		parent = clk_get_parent_by_index(clk, i);
		if (!parent)
			continue;

		parent_rate = __clk_get_rate(parent);
		now = clk_div_round_rate_even(div_hw, rate, &parent_rate);

		if (now <= rate && now > best) {
			best_parent = parent;
			best_prate = parent_rate;
			best = now;
		}
	}

out:
	if(best_prate)
		*best_parent_rate = best_prate;

	if (best_parent)
		*best_parent_p = best_parent;

	clk_debug("clk name = %s, determine rate = %lu, best = %lu\n"
			"\tbest_parent name = %s, best_prate = %lu\n",
			clk->name, rate, best,
			__clk_get_name(*best_parent_p), *best_parent_rate);

	return best;
}

static long clk_mux_with_evendiv_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_div_round_rate_even(hw, rate, prate);
}

const struct clk_ops clkops_rate_mux_with_evendiv = {
	.recalc_rate	= clk_divider_recalc_rate,
	.set_rate	= clk_divider_set_rate,
	.round_rate	= clk_mux_with_evendiv_round_rate,
	.determine_rate = clk_mux_with_evendiv_determine_rate,
};

static int clk_i2s_fracdiv_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	u32 numerator, denominator;
	struct clk_divider *div = to_clk_divider(hw);
	int i = 10;


	if(clk_fracdiv_get_config(rate, parent_rate,
				&numerator, &denominator) == 0) {
		while (i--) {
			writel((numerator - 1) << 16 | denominator, div->reg);
			mdelay(1);
			writel(numerator << 16 | denominator, div->reg);
			mdelay(1);
		}
		clk_debug("%s set rate=%lu,is ok\n", hw->clk->name, rate);
	} else {
		clk_err("clk_frac_div name=%s can't get rate=%lu\n",
				hw->clk->name, rate);
		return -EINVAL;
	}

	return 0;
}

const struct clk_ops clkops_rate_frac = {
	.recalc_rate	= clk_fracdiv_recalc,
	.round_rate	= clk_fracdiv_round_rate,
	.set_rate	= clk_fracdiv_set_rate,
};

const struct clk_ops clkops_rate_i2s_frac = {
	.recalc_rate	= clk_fracdiv_recalc,
	.round_rate	= clk_fracdiv_round_rate,
	.set_rate	= clk_i2s_fracdiv_set_rate,
};

static unsigned long clk_core_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	/* As parent rate could be changed in clk_core.set_rate
	 * ops, the passing_in parent_rate may not be the newest
	 * and we should use the parent->rate instead. As a side
	 * effect, we should NOT directly set clk_core's parent
	 * (apll) rate, otherwise we will get a wrong recalc rate
	 * with clk_core_recalc_rate.
	 */
	struct clk *parent = __clk_get_parent(hw->clk);

	return clk_divider_recalc_rate(hw, __clk_get_rate(parent));
}

static long clk_core_determine_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	struct clk *parent = __clk_get_parent(hw->clk);

	if (IS_ERR_OR_NULL(parent)) {
		clk_err("fail to get parent!\n");
		return 0;
	}

	return clk_round_rate(parent, rate);
}

static long clk_core_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_core_determine_rate(hw, rate, prate, NULL);
}

static int clk_core_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk *parent = __clk_get_parent(hw->clk);
	struct clk *grand_p = __clk_get_parent(parent);
	int ret;

	if (IS_ERR_OR_NULL(parent) || IS_ERR_OR_NULL(grand_p)) {
		clk_err("fail to get parent or grand_parent!\n");
		return -EINVAL;
	}

	ret = parent->ops->set_rate(parent->hw, rate, __clk_get_rate(grand_p));
	parent->rate = parent->ops->recalc_rate(parent->hw,
			__clk_get_rate(grand_p));

	return ret;
}

const struct clk_ops clkops_rate_core = {
	.recalc_rate	= clk_core_recalc_rate,
	.round_rate	= clk_core_round_rate,
	.set_rate	= clk_core_set_rate,
	.determine_rate = clk_core_determine_rate,
};

/* Clk_ops for the child clk of clk_core, for example core_periph in rk3188 */
const struct clk_ops clkops_rate_core_peri = {
	.recalc_rate	= clk_divider_recalc_rate,
	.round_rate	= clk_divider_round_rate,
	.set_rate	= NULL,
};

static unsigned long clk_ddr_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	/* Same as clk_core, we should NOT set clk_ddr's parent
	 * (dpll) rate directly as a side effect.
	 */
	return clk_core_recalc_rate(hw, parent_rate);
}

static long clk_ddr_determine_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	long best = 0;

	if (!ddr_round_rate) {
		/* Do nothing before ddr init */
		best = rate;//__clk_get_rate(hw->clk);
	} else {
		/* Func provided by ddr driver */
		best = ddr_round_rate(rate/MHZ) * MHZ;
	}

	clk_debug("%s: from %lu to %lu\n", __func__, rate, best);

	return best;
}

static long clk_ddr_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_ddr_determine_rate(hw, rate, prate, NULL);
}

static int clk_ddr_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk *parent = __clk_get_parent(hw->clk);
	struct clk *grand_p = __clk_get_parent(parent);


	/* Do nothing before ddr init */
	if (!ddr_change_freq)
		return 0;

	if (IS_ERR_OR_NULL(parent) || IS_ERR_OR_NULL(grand_p)) {
		clk_err("fail to get parent or grand_parent!\n");
		return -EINVAL;
	}

	clk_debug("%s: will set rate = %lu\n", __func__, rate);

	/* Func provided by ddr driver */
	ddr_change_freq(rate/MHZ);

	parent->rate = parent->ops->recalc_rate(parent->hw,
			__clk_get_rate(grand_p));

	return 0;
}

const struct clk_ops clkops_rate_ddr = {
	.recalc_rate	= clk_ddr_recalc_rate,
	.round_rate	= clk_ddr_round_rate,
	.set_rate	= clk_ddr_set_rate,
	.determine_rate = clk_ddr_determine_rate,
};

static unsigned long clk_3288_i2s_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return parent_rate;
}

static long clk_3288_i2s_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return rate;
}

static int clk_3288_i2s_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk *parent = __clk_get_parent(hw->clk);
	struct clk *grand_p = __clk_get_parent(parent);


	if (IS_ERR_OR_NULL(parent) || IS_ERR_OR_NULL(grand_p)) {
		return 0;
	}

	if (parent->ops->set_rate) {
		parent->ops->set_rate(parent->hw, rate/2, __clk_get_rate(grand_p));
		parent->ops->set_rate(parent->hw, rate, __clk_get_rate(grand_p));
	}

	return 0;
}

const struct clk_ops clkops_rate_3288_i2s = {
	.recalc_rate	= clk_3288_i2s_recalc_rate,
	.round_rate	= clk_3288_i2s_round_rate,
	.set_rate	= clk_3288_i2s_set_rate,
};

static bool usb480m_state = false;

static long clk_3288_usb480m_determine_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	if(rate == 0)
		return 0;
	else
		return 480*MHZ;
}

static long clk_3288_usb480m_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_3288_usb480m_determine_rate(hw, rate, prate, NULL);
}

static int clk_3288_usb480m_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	if(rate == 0)
		usb480m_state = false;
	else
		usb480m_state = true;

	return 0;
}

static unsigned long clk_3288_usb480m_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	if(usb480m_state)
		return 480*MHZ;
	else
		return 0;
}

const struct clk_ops clkops_rate_3288_usb480m = {
	.determine_rate = clk_3288_usb480m_determine_rate,
	.set_rate	= clk_3288_usb480m_set_rate,
	.round_rate	= clk_3288_usb480m_round_rate,
	.recalc_rate	= clk_3288_usb480m_recalc_rate,
};

#define RK3288_LIMIT_PLL_VIO0 (410*MHZ)

static long clk_3288_dclk_lcdc0_determine_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	struct clk *gpll = clk_get(NULL, "clk_gpll");
	struct clk *cpll = clk_get(NULL, "clk_cpll");
	unsigned long best, div, prate;


	if((rate <= (297*MHZ)) && ((297*MHZ)%rate == 0)) {
		*best_parent_p = gpll;
		best = rate;
		*best_parent_rate = 297*MHZ;
	} else {
		*best_parent_p = cpll;
		div = RK3288_LIMIT_PLL_VIO0/rate;
		prate = div * rate;
		*best_parent_rate = clk_round_rate(cpll, prate);
		best = (*best_parent_rate)/div;	
	}

	return best;
}

static long clk_3288_dclk_lcdc0_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_3288_dclk_lcdc0_determine_rate(hw, rate, prate, NULL);
}

static int clk_3288_dclk_lcdc0_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk* aclk_vio0 = clk_get(NULL, "aclk_vio0");
	struct clk* hclk_vio = clk_get(NULL, "hclk_vio");
	struct clk* parent;

	clk_divider_ops.set_rate(hw, rate, parent_rate);

	/* set aclk_vio */
	if(parent_rate	== 297*MHZ)
		parent = clk_get(NULL, "clk_gpll");
	else
		parent = clk_get(NULL, "clk_cpll");

	clk_set_parent(aclk_vio0, parent);
	clk_set_rate(aclk_vio0, __clk_get_rate(parent));
	clk_set_rate(hclk_vio, 100*MHZ);

	return 0;
}

const struct clk_ops clkops_rate_3288_dclk_lcdc0 = {
	.determine_rate = clk_3288_dclk_lcdc0_determine_rate,
	.set_rate	= clk_3288_dclk_lcdc0_set_rate,
	.round_rate	= clk_3288_dclk_lcdc0_round_rate,
	.recalc_rate	= clk_divider_recalc_rate,
};

#define RK3288_LIMIT_PLL_VIO1 (350*MHZ)

static long clk_3288_dclk_lcdc1_determine_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *best_parent_rate,
		struct clk **best_parent_p)
{
	struct clk *gpll = clk_get(NULL, "clk_gpll");
	struct clk *cpll = clk_get(NULL, "clk_cpll");
	unsigned long best, div, prate;


	if((rate <= (297*MHZ)) && ((297*MHZ)%rate == 0)) {
		*best_parent_p = gpll;
		best = rate;
		*best_parent_rate = 297*MHZ;
	} else {
		*best_parent_p = cpll;
		div = RK3288_LIMIT_PLL_VIO1/rate;
		prate = div * rate;
		*best_parent_rate = clk_round_rate(cpll, prate);
		best = (*best_parent_rate)/div;	
	}

	return best;
}

static long clk_3288_dclk_lcdc1_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	return clk_3288_dclk_lcdc1_determine_rate(hw, rate, prate, NULL);
}

static int clk_3288_dclk_lcdc1_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk* aclk_vio1 = clk_get(NULL, "aclk_vio1");
	struct clk* parent;

	clk_divider_ops.set_rate(hw, rate, parent_rate);

	/* set aclk_vio */
	if(parent_rate	== 297*MHZ)
		parent = clk_get(NULL, "clk_gpll");
	else
		parent = clk_get(NULL, "clk_cpll");

	clk_set_parent(aclk_vio1, parent);
	clk_set_rate(aclk_vio1, __clk_get_rate(parent));

	return 0;
}

const struct clk_ops clkops_rate_3288_dclk_lcdc1 = {
	.determine_rate = clk_3288_dclk_lcdc1_determine_rate,
	.set_rate	= clk_3288_dclk_lcdc1_set_rate,
	.round_rate	= clk_3288_dclk_lcdc1_round_rate,
	.recalc_rate	= clk_divider_recalc_rate,
};


struct clk_ops_table rk_clkops_rate_table[] = {
	{.index = CLKOPS_RATE_MUX_DIV,		.clk_ops = &clkops_rate_auto_parent},
	{.index = CLKOPS_RATE_EVENDIV,		.clk_ops = &clkops_rate_evendiv},
	{.index = CLKOPS_RATE_MUX_EVENDIV,	.clk_ops = &clkops_rate_mux_with_evendiv},
	{.index = CLKOPS_RATE_I2S_FRAC,		.clk_ops = &clkops_rate_i2s_frac},
	{.index = CLKOPS_RATE_FRAC,		.clk_ops = &clkops_rate_frac},
	{.index = CLKOPS_RATE_CORE,		.clk_ops = &clkops_rate_core},
	{.index = CLKOPS_RATE_CORE_CHILD,	.clk_ops = &clkops_rate_core_peri},
	{.index = CLKOPS_RATE_DDR,		.clk_ops = &clkops_rate_ddr},
	{.index = CLKOPS_RATE_RK3288_I2S,	.clk_ops = &clkops_rate_3288_i2s},
	{.index = CLKOPS_RATE_RK3288_USB480M,	.clk_ops = &clkops_rate_3288_usb480m},
	{.index = CLKOPS_RATE_RK3288_DCLK_LCDC0,.clk_ops = &clkops_rate_3288_dclk_lcdc0},
	{.index = CLKOPS_RATE_RK3288_DCLK_LCDC1,.clk_ops = &clkops_rate_3288_dclk_lcdc1},
	{.index = CLKOPS_RATE_I2S,		.clk_ops = NULL},
	{.index = CLKOPS_RATE_CIFOUT,		.clk_ops = NULL},
	{.index = CLKOPS_RATE_UART,		.clk_ops = NULL},
	{.index = CLKOPS_RATE_HSADC,		.clk_ops = NULL},
	{.index = CLKOPS_RATE_MAC_REF,		.clk_ops = NULL},
	{.index = CLKOPS_TABLE_END,		.clk_ops = NULL},
};

const struct clk_ops *rk_get_clkops(unsigned int idx)
{
	int i = 0;
	unsigned int now_idx;

	while(1){
		now_idx = rk_clkops_rate_table[i].index;

		if ((now_idx == idx) || (now_idx == CLKOPS_TABLE_END))
			return rk_clkops_rate_table[i].clk_ops;

		i++;
	}
}
EXPORT_SYMBOL_GPL(rk_get_clkops);
