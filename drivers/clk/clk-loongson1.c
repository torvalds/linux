// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Clock driver for Loongson-1 SoC
 *
 * Copyright (C) 2012-2023 Keguang Zhang <keguang.zhang@gmail.com>
 */

#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/printk.h>

#include <dt-bindings/clock/loongson,ls1x-clk.h>

/* Loongson 1 Clock Register Definitions */
#define CLK_PLL_FREQ		0x0
#define CLK_PLL_DIV		0x4

static DEFINE_SPINLOCK(ls1x_clk_div_lock);

struct ls1x_clk_pll_data {
	u32 fixed;
	u8 shift;
	u8 int_shift;
	u8 int_width;
	u8 frac_shift;
	u8 frac_width;
};

struct ls1x_clk_div_data {
	u8 shift;
	u8 width;
	unsigned long flags;
	const struct clk_div_table *table;
	u8 bypass_shift;
	u8 bypass_inv;
	spinlock_t *lock;	/* protect access to DIV registers */
};

struct ls1x_clk {
	void __iomem *reg;
	unsigned int offset;
	struct clk_hw hw;
	const void *data;
};

#define to_ls1x_clk(_hw) container_of(_hw, struct ls1x_clk, hw)

static inline unsigned long ls1x_pll_rate_part(unsigned int val,
					       unsigned int shift,
					       unsigned int width)
{
	return (val & GENMASK(shift + width, shift)) >> shift;
}

static unsigned long ls1x_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct ls1x_clk *ls1x_clk = to_ls1x_clk(hw);
	const struct ls1x_clk_pll_data *d = ls1x_clk->data;
	u32 val, rate;

	val = readl(ls1x_clk->reg);
	rate = d->fixed;
	rate += ls1x_pll_rate_part(val, d->int_shift, d->int_width);
	if (d->frac_width)
		rate += ls1x_pll_rate_part(val, d->frac_shift, d->frac_width);
	rate *= parent_rate;
	rate >>= d->shift;

	return rate;
}

static const struct clk_ops ls1x_pll_clk_ops = {
	.recalc_rate = ls1x_pll_recalc_rate,
};

static unsigned long ls1x_divider_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct ls1x_clk *ls1x_clk = to_ls1x_clk(hw);
	const struct ls1x_clk_div_data *d = ls1x_clk->data;
	unsigned int val;

	val = readl(ls1x_clk->reg) >> d->shift;
	val &= clk_div_mask(d->width);

	return divider_recalc_rate(hw, parent_rate, val, d->table,
				   d->flags, d->width);
}

static int ls1x_divider_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct ls1x_clk *ls1x_clk = to_ls1x_clk(hw);
	const struct ls1x_clk_div_data *d = ls1x_clk->data;

	req->rate = divider_round_rate(hw, req->rate, &req->best_parent_rate,
				       d->table, d->width, d->flags);

	return 0;
}

static int ls1x_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct ls1x_clk *ls1x_clk = to_ls1x_clk(hw);
	const struct ls1x_clk_div_data *d = ls1x_clk->data;
	int val, div_val;
	unsigned long flags = 0;

	div_val = divider_get_val(rate, parent_rate, d->table,
				  d->width, d->flags);
	if (div_val < 0)
		return div_val;

	spin_lock_irqsave(d->lock, flags);

	/* Bypass the clock */
	val = readl(ls1x_clk->reg);
	if (d->bypass_inv)
		val &= ~BIT(d->bypass_shift);
	else
		val |= BIT(d->bypass_shift);
	writel(val, ls1x_clk->reg);

	val = readl(ls1x_clk->reg);
	val &= ~(clk_div_mask(d->width) << d->shift);
	val |= (u32)div_val << d->shift;
	writel(val, ls1x_clk->reg);

	/* Restore the clock */
	val = readl(ls1x_clk->reg);
	if (d->bypass_inv)
		val |= BIT(d->bypass_shift);
	else
		val &= ~BIT(d->bypass_shift);
	writel(val, ls1x_clk->reg);

	spin_unlock_irqrestore(d->lock, flags);

	return 0;
}

static const struct clk_ops ls1x_clk_divider_ops = {
	.recalc_rate = ls1x_divider_recalc_rate,
	.determine_rate = ls1x_divider_determine_rate,
	.set_rate = ls1x_divider_set_rate,
};

#define LS1X_CLK_PLL(_name, _offset, _fixed, _shift,			\
		     f_shift, f_width, i_shift, i_width)		\
struct ls1x_clk _name = {						\
	.offset = (_offset),						\
	.data = &(const struct ls1x_clk_pll_data) {			\
		.fixed = (_fixed),					\
		.shift = (_shift),					\
		.int_shift = (i_shift),					\
		.int_width = (i_width),					\
		.frac_shift = (f_shift),				\
		.frac_width = (f_width),				\
	},								\
	.hw.init = &(const struct clk_init_data) {			\
		.name = #_name,						\
		.ops = &ls1x_pll_clk_ops,				\
		.parent_data = &(const struct clk_parent_data) {	\
			.fw_name = "xtal",				\
			.name = "xtal",					\
			.index = -1,					\
		},							\
		.num_parents = 1,					\
	},								\
}

#define LS1X_CLK_DIV(_name, _pname, _offset, _shift, _width,		\
		     _table, _bypass_shift, _bypass_inv, _flags)	\
struct ls1x_clk _name = {						\
	.offset = (_offset),						\
	.data = &(const struct ls1x_clk_div_data){			\
		.shift = (_shift),					\
		.width = (_width),					\
		.table = (_table),					\
		.flags = (_flags),					\
		.bypass_shift = (_bypass_shift),			\
		.bypass_inv = (_bypass_inv),				\
		.lock = &ls1x_clk_div_lock,				\
	},								\
	.hw.init = &(const struct clk_init_data) {			\
		.name = #_name,						\
		.ops = &ls1x_clk_divider_ops,				\
		.parent_hws = (const struct clk_hw *[]) { _pname },	\
		.num_parents = 1,					\
		.flags = CLK_GET_RATE_NOCACHE,				\
	},								\
}

static LS1X_CLK_PLL(ls1b_clk_pll, CLK_PLL_FREQ, 12, 1, 0, 5, 0, 0);
static LS1X_CLK_DIV(ls1b_clk_cpu, &ls1b_clk_pll.hw, CLK_PLL_DIV,
		    20, 4, NULL, 8, 0,
		    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ROUND_CLOSEST);
static LS1X_CLK_DIV(ls1b_clk_dc, &ls1b_clk_pll.hw, CLK_PLL_DIV,
		    26, 4, NULL, 12, 0, CLK_DIVIDER_ONE_BASED);
static LS1X_CLK_DIV(ls1b_clk_ahb, &ls1b_clk_pll.hw, CLK_PLL_DIV,
		    14, 4, NULL, 10, 0, CLK_DIVIDER_ONE_BASED);
static CLK_FIXED_FACTOR(ls1b_clk_apb, "ls1b_clk_apb", "ls1b_clk_ahb", 2, 1,
			CLK_SET_RATE_PARENT);

static struct clk_hw_onecell_data ls1b_clk_hw_data = {
	.hws = {
		[LS1X_CLKID_PLL] = &ls1b_clk_pll.hw,
		[LS1X_CLKID_CPU] = &ls1b_clk_cpu.hw,
		[LS1X_CLKID_DC] = &ls1b_clk_dc.hw,
		[LS1X_CLKID_AHB] = &ls1b_clk_ahb.hw,
		[LS1X_CLKID_APB] = &ls1b_clk_apb.hw,
	},
	.num = CLK_NR_CLKS,
};

static const struct clk_div_table ls1c_ahb_div_table[] = {
	[0] = { .val = 0, .div = 2 },
	[1] = { .val = 1, .div = 4 },
	[2] = { .val = 2, .div = 3 },
	[3] = { .val = 3, .div = 3 },
	[4] = { /* sentinel */ }
};

static LS1X_CLK_PLL(ls1c_clk_pll, CLK_PLL_FREQ, 0, 2, 8, 8, 16, 8);
static LS1X_CLK_DIV(ls1c_clk_cpu, &ls1c_clk_pll.hw, CLK_PLL_DIV,
		    8, 7, NULL, 0, 1,
		    CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ROUND_CLOSEST);
static LS1X_CLK_DIV(ls1c_clk_dc, &ls1c_clk_pll.hw, CLK_PLL_DIV,
		    24, 7, NULL, 4, 1, CLK_DIVIDER_ONE_BASED);
static LS1X_CLK_DIV(ls1c_clk_ahb, &ls1c_clk_cpu.hw, CLK_PLL_FREQ,
		    0, 2, ls1c_ahb_div_table, 0, 0, CLK_DIVIDER_ALLOW_ZERO);
static CLK_FIXED_FACTOR(ls1c_clk_apb, "ls1c_clk_apb", "ls1c_clk_ahb", 1, 1,
			CLK_SET_RATE_PARENT);

static struct clk_hw_onecell_data ls1c_clk_hw_data = {
	.hws = {
		[LS1X_CLKID_PLL] = &ls1c_clk_pll.hw,
		[LS1X_CLKID_CPU] = &ls1c_clk_cpu.hw,
		[LS1X_CLKID_DC] = &ls1c_clk_dc.hw,
		[LS1X_CLKID_AHB] = &ls1c_clk_ahb.hw,
		[LS1X_CLKID_APB] = &ls1c_clk_apb.hw,
	},
	.num = CLK_NR_CLKS,
};

static void __init ls1x_clk_init(struct device_node *np,
				 struct clk_hw_onecell_data *hw_data)
{
	struct ls1x_clk *ls1x_clk;
	void __iomem *reg;
	int i, ret;

	reg = of_iomap(np, 0);
	if (!reg) {
		pr_err("Unable to map base for %pOF\n", np);
		return;
	}

	for (i = 0; i < hw_data->num; i++) {
		/* array might be sparse */
		if (!hw_data->hws[i])
			continue;

		if (i != LS1X_CLKID_APB) {
			ls1x_clk = to_ls1x_clk(hw_data->hws[i]);
			ls1x_clk->reg = reg + ls1x_clk->offset;
		}

		ret = of_clk_hw_register(np, hw_data->hws[i]);
		if (ret)
			goto err;
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, hw_data);
	if (!ret)
		return;

err:
	pr_err("Failed to register %pOF\n", np);

	while (--i >= 0)
		clk_hw_unregister(hw_data->hws[i]);

	iounmap(reg);
}

static void __init ls1b_clk_init(struct device_node *np)
{
	return ls1x_clk_init(np, &ls1b_clk_hw_data);
}

static void __init ls1c_clk_init(struct device_node *np)
{
	return ls1x_clk_init(np, &ls1c_clk_hw_data);
}

CLK_OF_DECLARE(ls1b_clk, "loongson,ls1b-clk", ls1b_clk_init);
CLK_OF_DECLARE(ls1c_clk, "loongson,ls1c-clk", ls1c_clk_init);
