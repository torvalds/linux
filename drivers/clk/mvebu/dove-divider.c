// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell Dove PMU Core PLL divider driver
 *
 * Cleaned up by substantially rewriting, and converted to DT by
 * Russell King.  Origin is not known.
 */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "dove-divider.h"

struct dove_clk {
	const char *name;
	struct clk_hw hw;
	void __iomem *base;
	spinlock_t *lock;
	u8 div_bit_start;
	u8 div_bit_end;
	u8 div_bit_load;
	u8 div_bit_size;
	u32 *divider_table;
};

enum {
	DIV_CTRL0 = 0,
	DIV_CTRL1 = 4,
	DIV_CTRL1_N_RESET_MASK = BIT(10),
};

#define to_dove_clk(hw) container_of(hw, struct dove_clk, hw)

static void dove_load_divider(void __iomem *base, u32 val, u32 mask, u32 load)
{
	u32 v;

	v = readl_relaxed(base + DIV_CTRL1) | DIV_CTRL1_N_RESET_MASK;
	writel_relaxed(v, base + DIV_CTRL1);

	v = (readl_relaxed(base + DIV_CTRL0) & ~(mask | load)) | val;
	writel_relaxed(v, base + DIV_CTRL0);
	writel_relaxed(v | load, base + DIV_CTRL0);
	ndelay(250);
	writel_relaxed(v, base + DIV_CTRL0);
}

static unsigned int dove_get_divider(struct dove_clk *dc)
{
	unsigned int divider;
	u32 val;

	val = readl_relaxed(dc->base + DIV_CTRL0);
	val >>= dc->div_bit_start;

	divider = val & ~(~0 << dc->div_bit_size);

	if (dc->divider_table)
		divider = dc->divider_table[divider];

	return divider;
}

static int dove_calc_divider(const struct dove_clk *dc, unsigned long rate,
			     unsigned long parent_rate, bool set)
{
	unsigned int divider, max;

	divider = DIV_ROUND_CLOSEST(parent_rate, rate);

	if (dc->divider_table) {
		unsigned int i;

		for (i = 0; dc->divider_table[i]; i++)
			if (divider == dc->divider_table[i]) {
				divider = i;
				break;
			}

		if (!dc->divider_table[i])
			return -EINVAL;
	} else {
		max = 1 << dc->div_bit_size;

		if (set && (divider == 0 || divider >= max))
			return -EINVAL;
		if (divider >= max)
			divider = max - 1;
		else if (divider == 0)
			divider = 1;
	}

	return divider;
}

static unsigned long dove_recalc_rate(struct clk_hw *hw, unsigned long parent)
{
	struct dove_clk *dc = to_dove_clk(hw);
	unsigned int divider = dove_get_divider(dc);
	unsigned long rate = DIV_ROUND_CLOSEST(parent, divider);

	pr_debug("%s(): %s divider=%u parent=%lu rate=%lu\n",
		 __func__, dc->name, divider, parent, rate);

	return rate;
}

static long dove_round_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long *parent)
{
	struct dove_clk *dc = to_dove_clk(hw);
	unsigned long parent_rate = *parent;
	int divider;

	divider = dove_calc_divider(dc, rate, parent_rate, false);
	if (divider < 0)
		return divider;

	rate = DIV_ROUND_CLOSEST(parent_rate, divider);

	pr_debug("%s(): %s divider=%u parent=%lu rate=%lu\n",
		 __func__, dc->name, divider, parent_rate, rate);

	return rate;
}

static int dove_set_clock(struct clk_hw *hw, unsigned long rate,
			  unsigned long parent_rate)
{
	struct dove_clk *dc = to_dove_clk(hw);
	u32 mask, load, div;
	int divider;

	divider = dove_calc_divider(dc, rate, parent_rate, true);
	if (divider < 0)
		return divider;

	pr_debug("%s(): %s divider=%u parent=%lu rate=%lu\n",
		 __func__, dc->name, divider, parent_rate, rate);

	div = (u32)divider << dc->div_bit_start;
	mask = ~(~0 << dc->div_bit_size) << dc->div_bit_start;
	load = BIT(dc->div_bit_load);

	spin_lock(dc->lock);
	dove_load_divider(dc->base, div, mask, load);
	spin_unlock(dc->lock);

	return 0;
}

static const struct clk_ops dove_divider_ops = {
	.set_rate	= dove_set_clock,
	.round_rate	= dove_round_rate,
	.recalc_rate	= dove_recalc_rate,
};

static struct clk *clk_register_dove_divider(struct device *dev,
	struct dove_clk *dc, const char **parent_names, size_t num_parents,
	void __iomem *base)
{
	char name[32];
	struct clk_init_data init = {
		.name = name,
		.ops = &dove_divider_ops,
		.parent_names = parent_names,
		.num_parents = num_parents,
	};

	strlcpy(name, dc->name, sizeof(name));

	dc->hw.init = &init;
	dc->base = base;
	dc->div_bit_size = dc->div_bit_end - dc->div_bit_start + 1;

	return clk_register(dev, &dc->hw);
}

static DEFINE_SPINLOCK(dove_divider_lock);

static u32 axi_divider[] = {-1, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9, 0};

static struct dove_clk dove_hw_clocks[4] = {
	{
		.name = "axi",
		.lock = &dove_divider_lock,
		.div_bit_start = 1,
		.div_bit_end = 6,
		.div_bit_load = 7,
		.divider_table = axi_divider,
	}, {
		.name = "gpu",
		.lock = &dove_divider_lock,
		.div_bit_start = 8,
		.div_bit_end = 13,
		.div_bit_load = 14,
	}, {
		.name = "vmeta",
		.lock = &dove_divider_lock,
		.div_bit_start = 15,
		.div_bit_end = 20,
		.div_bit_load = 21,
	}, {
		.name = "lcd",
		.lock = &dove_divider_lock,
		.div_bit_start = 22,
		.div_bit_end = 27,
		.div_bit_load = 28,
	},
};

static const char *core_pll[] = {
	"core-pll",
};

static int dove_divider_init(struct device *dev, void __iomem *base,
	struct clk **clks)
{
	struct clk *clk;
	int i;

	/*
	 * Create the core PLL clock.  We treat this as a fixed rate
	 * clock as we don't know any better, and documentation is sparse.
	 */
	clk = clk_register_fixed_rate(dev, core_pll[0], NULL, 0, 2000000000UL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	for (i = 0; i < ARRAY_SIZE(dove_hw_clocks); i++)
		clks[i] = clk_register_dove_divider(dev, &dove_hw_clocks[i],
						    core_pll,
						    ARRAY_SIZE(core_pll), base);

	return 0;
}

static struct clk *dove_divider_clocks[4];

static struct clk_onecell_data dove_divider_data = {
	.clks = dove_divider_clocks,
	.clk_num = ARRAY_SIZE(dove_divider_clocks),
};

void __init dove_divider_clk_init(struct device_node *np)
{
	void __iomem *base;

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	if (WARN_ON(dove_divider_init(NULL, base, dove_divider_clocks))) {
		iounmap(base);
		return;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &dove_divider_data);
}
