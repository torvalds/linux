// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Socionext Inc.
 * Copyright (C) 2016 Linaro Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define M10V_CLKSEL1		0x0
#define CLKSEL(n)	(((n) - 1) * 4 + M10V_CLKSEL1)

#define M10V_PLL1		"pll1"
#define M10V_PLL1DIV2		"pll1-2"
#define M10V_PLL2		"pll2"
#define M10V_PLL2DIV2		"pll2-2"
#define M10V_PLL6		"pll6"
#define M10V_PLL6DIV2		"pll6-2"
#define M10V_PLL6DIV3		"pll6-3"
#define M10V_PLL7		"pll7"
#define M10V_PLL7DIV2		"pll7-2"
#define M10V_PLL7DIV5		"pll7-5"
#define M10V_PLL9		"pll9"
#define M10V_PLL10		"pll10"
#define M10V_PLL10DIV2		"pll10-2"
#define M10V_PLL11		"pll11"

#define M10V_SPI_PARENT0	"spi-parent0"
#define M10V_SPI_PARENT1	"spi-parent1"
#define M10V_SPI_PARENT2	"spi-parent2"
#define M10V_UHS1CLK2_PARENT0	"uhs1clk2-parent0"
#define M10V_UHS1CLK2_PARENT1	"uhs1clk2-parent1"
#define M10V_UHS1CLK2_PARENT2	"uhs1clk2-parent2"
#define M10V_UHS1CLK1_PARENT0	"uhs1clk1-parent0"
#define M10V_UHS1CLK1_PARENT1	"uhs1clk1-parent1"
#define M10V_NFCLK_PARENT0	"nfclk-parent0"
#define M10V_NFCLK_PARENT1	"nfclk-parent1"
#define M10V_NFCLK_PARENT2	"nfclk-parent2"
#define M10V_NFCLK_PARENT3	"nfclk-parent3"
#define M10V_NFCLK_PARENT4	"nfclk-parent4"
#define M10V_NFCLK_PARENT5	"nfclk-parent5"

#define M10V_DCHREQ		1
#define M10V_UPOLL_RATE		1
#define M10V_UTIMEOUT		250

#define M10V_EMMCCLK_ID		0
#define M10V_ACLK_ID		1
#define M10V_HCLK_ID		2
#define M10V_PCLK_ID		3
#define M10V_RCLK_ID		4
#define M10V_SPICLK_ID		5
#define M10V_NFCLK_ID		6
#define M10V_UHS1CLK2_ID	7
#define M10V_NUM_CLKS		8

#define to_m10v_div(_hw)        container_of(_hw, struct m10v_clk_divider, hw)

static struct clk_hw_onecell_data *m10v_clk_data;

static DEFINE_SPINLOCK(m10v_crglock);

struct m10v_clk_div_factors {
	const char			*name;
	const char			*parent_name;
	u32				offset;
	u8				shift;
	u8				width;
	const struct clk_div_table	*table;
	unsigned long			div_flags;
	int				onecell_idx;
};

struct m10v_clk_div_fixed_data {
	const char	*name;
	const char	*parent_name;
	u8		div;
	u8		mult;
	int		onecell_idx;
};

struct m10v_clk_mux_factors {
	const char		*name;
	const char * const	*parent_names;
	u8			num_parents;
	u32			offset;
	u8			shift;
	u8			mask;
	u32			*table;
	unsigned long		mux_flags;
	int			onecell_idx;
};

static const struct clk_div_table emmcclk_table[] = {
	{ .val = 0, .div = 8 },
	{ .val = 1, .div = 9 },
	{ .val = 2, .div = 10 },
	{ .val = 3, .div = 15 },
	{ .div = 0 },
};

static const struct clk_div_table mclk400_table[] = {
	{ .val = 1, .div = 2 },
	{ .val = 3, .div = 4 },
	{ .div = 0 },
};

static const struct clk_div_table mclk200_table[] = {
	{ .val = 3, .div = 4 },
	{ .val = 7, .div = 8 },
	{ .div = 0 },
};

static const struct clk_div_table aclk400_table[] = {
	{ .val = 1, .div = 2 },
	{ .val = 3, .div = 4 },
	{ .div = 0 },
};

static const struct clk_div_table aclk300_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 3 },
	{ .div = 0 },
};

static const struct clk_div_table aclk_table[] = {
	{ .val = 3, .div = 4 },
	{ .val = 7, .div = 8 },
	{ .div = 0 },
};

static const struct clk_div_table aclkexs_table[] = {
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 5 },
	{ .val = 5, .div = 6 },
	{ .val = 7, .div = 8 },
	{ .div = 0 },
};

static const struct clk_div_table hclk_table[] = {
	{ .val = 7, .div = 8 },
	{ .val = 15, .div = 16 },
	{ .div = 0 },
};

static const struct clk_div_table hclkbmh_table[] = {
	{ .val = 3, .div = 4 },
	{ .val = 7, .div = 8 },
	{ .div = 0 },
};

static const struct clk_div_table pclk_table[] = {
	{ .val = 15, .div = 16 },
	{ .val = 31, .div = 32 },
	{ .div = 0 },
};

static const struct clk_div_table rclk_table[] = {
	{ .val = 0, .div = 8 },
	{ .val = 1, .div = 16 },
	{ .val = 2, .div = 24 },
	{ .val = 3, .div = 32 },
	{ .div = 0 },
};

static const struct clk_div_table uhs1clk0_table[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 3 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ .val = 4, .div = 16 },
	{ .div = 0 },
};

static const struct clk_div_table uhs2clk_table[] = {
	{ .val = 0, .div = 9 },
	{ .val = 1, .div = 10 },
	{ .val = 2, .div = 11 },
	{ .val = 3, .div = 12 },
	{ .val = 4, .div = 13 },
	{ .val = 5, .div = 14 },
	{ .val = 6, .div = 16 },
	{ .val = 7, .div = 18 },
	{ .div = 0 },
};

static u32 spi_mux_table[] = {0, 1, 2};
static const char * const spi_mux_names[] = {
	M10V_SPI_PARENT0, M10V_SPI_PARENT1, M10V_SPI_PARENT2
};

static u32 uhs1clk2_mux_table[] = {2, 3, 4, 8};
static const char * const uhs1clk2_mux_names[] = {
	M10V_UHS1CLK2_PARENT0, M10V_UHS1CLK2_PARENT1,
	M10V_UHS1CLK2_PARENT2, M10V_PLL6DIV2
};

static u32 uhs1clk1_mux_table[] = {3, 4, 8};
static const char * const uhs1clk1_mux_names[] = {
	M10V_UHS1CLK1_PARENT0, M10V_UHS1CLK1_PARENT1, M10V_PLL6DIV2
};

static u32 nfclk_mux_table[] = {0, 1, 2, 3, 4, 8};
static const char * const nfclk_mux_names[] = {
	M10V_NFCLK_PARENT0, M10V_NFCLK_PARENT1, M10V_NFCLK_PARENT2,
	M10V_NFCLK_PARENT3, M10V_NFCLK_PARENT4, M10V_NFCLK_PARENT5
};

static const struct m10v_clk_div_fixed_data m10v_pll_fixed_data[] = {
	{M10V_PLL1, NULL, 1, 40, -1},
	{M10V_PLL2, NULL, 1, 30, -1},
	{M10V_PLL6, NULL, 1, 35, -1},
	{M10V_PLL7, NULL, 1, 40, -1},
	{M10V_PLL9, NULL, 1, 33, -1},
	{M10V_PLL10, NULL, 5, 108, -1},
	{M10V_PLL10DIV2, M10V_PLL10, 2, 1, -1},
	{M10V_PLL11, NULL, 2, 75, -1},
};

static const struct m10v_clk_div_fixed_data m10v_div_fixed_data[] = {
	{"usb2", NULL, 2, 1, -1},
	{"pcisuppclk", NULL, 20, 1, -1},
	{M10V_PLL1DIV2, M10V_PLL1, 2, 1, -1},
	{M10V_PLL2DIV2, M10V_PLL2, 2, 1, -1},
	{M10V_PLL6DIV2, M10V_PLL6, 2, 1, -1},
	{M10V_PLL6DIV3, M10V_PLL6, 3, 1, -1},
	{M10V_PLL7DIV2, M10V_PLL7, 2, 1, -1},
	{M10V_PLL7DIV5, M10V_PLL7, 5, 1, -1},
	{"ca7wd", M10V_PLL2DIV2, 12, 1, -1},
	{"pclkca7wd", M10V_PLL1DIV2, 16, 1, -1},
	{M10V_SPI_PARENT0, M10V_PLL10DIV2, 2, 1, -1},
	{M10V_SPI_PARENT1, M10V_PLL10DIV2, 4, 1, -1},
	{M10V_SPI_PARENT2, M10V_PLL7DIV2, 8, 1, -1},
	{M10V_UHS1CLK2_PARENT0, M10V_PLL7, 4, 1, -1},
	{M10V_UHS1CLK2_PARENT1, M10V_PLL7, 8, 1, -1},
	{M10V_UHS1CLK2_PARENT2, M10V_PLL7, 16, 1, -1},
	{M10V_UHS1CLK1_PARENT0, M10V_PLL7, 8, 1, -1},
	{M10V_UHS1CLK1_PARENT1, M10V_PLL7, 16, 1, -1},
	{M10V_NFCLK_PARENT0, M10V_PLL7DIV2, 8, 1, -1},
	{M10V_NFCLK_PARENT1, M10V_PLL7DIV2, 10, 1, -1},
	{M10V_NFCLK_PARENT2, M10V_PLL7DIV2, 13, 1, -1},
	{M10V_NFCLK_PARENT3, M10V_PLL7DIV2, 16, 1, -1},
	{M10V_NFCLK_PARENT4, M10V_PLL7DIV2, 40, 1, -1},
	{M10V_NFCLK_PARENT5, M10V_PLL7DIV5, 10, 1, -1},
};

static const struct m10v_clk_div_factors m10v_div_factor_data[] = {
	{"emmc", M10V_PLL11, CLKSEL(1), 28, 3, emmcclk_table, 0,
		M10V_EMMCCLK_ID},
	{"mclk400", M10V_PLL1DIV2, CLKSEL(10), 7, 3, mclk400_table, 0, -1},
	{"mclk200", M10V_PLL1DIV2, CLKSEL(10), 3, 4, mclk200_table, 0, -1},
	{"aclk400", M10V_PLL1DIV2, CLKSEL(10), 0, 3, aclk400_table, 0, -1},
	{"aclk300", M10V_PLL2DIV2, CLKSEL(12), 0, 2, aclk300_table, 0, -1},
	{"aclk", M10V_PLL1DIV2, CLKSEL(9), 20, 4, aclk_table, 0, M10V_ACLK_ID},
	{"aclkexs", M10V_PLL1DIV2, CLKSEL(9), 16, 4, aclkexs_table, 0, -1},
	{"hclk", M10V_PLL1DIV2, CLKSEL(9), 7, 5, hclk_table, 0, M10V_HCLK_ID},
	{"hclkbmh", M10V_PLL1DIV2, CLKSEL(9), 12, 4, hclkbmh_table, 0, -1},
	{"pclk", M10V_PLL1DIV2, CLKSEL(9), 0, 7, pclk_table, 0, M10V_PCLK_ID},
	{"uhs1clk0", M10V_PLL7, CLKSEL(1), 3, 5, uhs1clk0_table, 0, -1},
	{"uhs2clk", M10V_PLL6DIV3, CLKSEL(1), 18, 4, uhs2clk_table, 0, -1},
};

static const struct m10v_clk_mux_factors m10v_mux_factor_data[] = {
	{"spi", spi_mux_names, ARRAY_SIZE(spi_mux_names),
		CLKSEL(8), 3, 7, spi_mux_table, 0, M10V_SPICLK_ID},
	{"uhs1clk2", uhs1clk2_mux_names, ARRAY_SIZE(uhs1clk2_mux_names),
		CLKSEL(1), 13, 31, uhs1clk2_mux_table, 0, M10V_UHS1CLK2_ID},
	{"uhs1clk1", uhs1clk1_mux_names, ARRAY_SIZE(uhs1clk1_mux_names),
		CLKSEL(1), 8, 31, uhs1clk1_mux_table, 0, -1},
	{"nfclk", nfclk_mux_names, ARRAY_SIZE(nfclk_mux_names),
		CLKSEL(1), 22, 127, nfclk_mux_table, 0, M10V_NFCLK_ID},
};

static u8 m10v_mux_get_parent(struct clk_hw *hw)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val;

	val = readl(mux->reg) >> mux->shift;
	val &= mux->mask;

	return clk_mux_val_to_index(hw, mux->table, mux->flags, val);
}

static int m10v_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_mux *mux = to_clk_mux(hw);
	u32 val = clk_mux_index_to_val(mux->table, mux->flags, index);
	unsigned long flags = 0;
	u32 reg;
	u32 write_en = BIT(fls(mux->mask) - 1);

	if (mux->lock)
		spin_lock_irqsave(mux->lock, flags);
	else
		__acquire(mux->lock);

	reg = readl(mux->reg);
	reg &= ~(mux->mask << mux->shift);

	val = (val | write_en) << mux->shift;
	reg |= val;
	writel(reg, mux->reg);

	if (mux->lock)
		spin_unlock_irqrestore(mux->lock, flags);
	else
		__release(mux->lock);

	return 0;
}

static const struct clk_ops m10v_mux_ops = {
	.get_parent = m10v_mux_get_parent,
	.set_parent = m10v_mux_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_hw *m10v_clk_hw_register_mux(struct device *dev,
			const char *name, const char * const *parent_names,
			u8 num_parents, unsigned long flags, void __iomem *reg,
			u8 shift, u32 mask, u8 clk_mux_flags, u32 *table,
			spinlock_t *lock)
{
	struct clk_mux *mux;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &m10v_mux_ops;
	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	mux->reg = reg;
	mux->shift = shift;
	mux->mask = mask;
	mux->flags = clk_mux_flags;
	mux->lock = lock;
	mux->table = table;
	mux->hw.init = &init;

	hw = &mux->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(mux);
		hw = ERR_PTR(ret);
	}

	return hw;

}

struct m10v_clk_divider {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		shift;
	u8		width;
	u8		flags;
	const struct clk_div_table	*table;
	spinlock_t	*lock;
	void __iomem	*write_valid_reg;
};

static unsigned long m10v_clk_divider_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct m10v_clk_divider *divider = to_m10v_div(hw);
	unsigned int val;

	val = readl(divider->reg) >> divider->shift;
	val &= clk_div_mask(divider->width);

	return divider_recalc_rate(hw, parent_rate, val, divider->table,
				   divider->flags, divider->width);
}

static long m10v_clk_divider_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct m10v_clk_divider *divider = to_m10v_div(hw);

	/* if read only, just return current value */
	if (divider->flags & CLK_DIVIDER_READ_ONLY) {
		u32 val;

		val = readl(divider->reg) >> divider->shift;
		val &= clk_div_mask(divider->width);

		return divider_ro_round_rate(hw, rate, prate, divider->table,
					     divider->width, divider->flags,
					     val);
	}

	return divider_round_rate(hw, rate, prate, divider->table,
				  divider->width, divider->flags);
}

static int m10v_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct m10v_clk_divider *divider = to_m10v_div(hw);
	int value;
	unsigned long flags = 0;
	u32 val;
	u32 write_en = BIT(divider->width - 1);

	value = divider_get_val(rate, parent_rate, divider->table,
				divider->width, divider->flags);
	if (value < 0)
		return value;

	if (divider->lock)
		spin_lock_irqsave(divider->lock, flags);
	else
		__acquire(divider->lock);

	val = readl(divider->reg);
	val &= ~(clk_div_mask(divider->width) << divider->shift);

	val |= ((u32)value | write_en) << divider->shift;
	writel(val, divider->reg);

	if (divider->write_valid_reg) {
		writel(M10V_DCHREQ, divider->write_valid_reg);
		if (readl_poll_timeout(divider->write_valid_reg, val,
			!val, M10V_UPOLL_RATE, M10V_UTIMEOUT))
			pr_err("%s:%s couldn't stabilize\n",
				__func__, divider->hw.init->name);
	}

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);
	else
		__release(divider->lock);

	return 0;
}

static const struct clk_ops m10v_clk_divider_ops = {
	.recalc_rate = m10v_clk_divider_recalc_rate,
	.round_rate = m10v_clk_divider_round_rate,
	.set_rate = m10v_clk_divider_set_rate,
};

static struct clk_hw *m10v_clk_hw_register_divider(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		u8 clk_divider_flags, const struct clk_div_table *table,
		spinlock_t *lock, void __iomem *write_valid_reg)
{
	struct m10v_clk_divider *div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &m10v_clk_divider_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	div->reg = reg;
	div->shift = shift;
	div->width = width;
	div->flags = clk_divider_flags;
	div->lock = lock;
	div->hw.init = &init;
	div->table = table;
	div->write_valid_reg = write_valid_reg;

	/* register the clock */
	hw = &div->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(div);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static void m10v_reg_div_pre(const struct m10v_clk_div_factors *factors,
			     struct clk_hw_onecell_data *clk_data,
			     void __iomem *base)
{
	struct clk_hw *hw;
	void __iomem *write_valid_reg;

	/*
	 * The registers on CLKSEL(9) or CLKSEL(10) need additional
	 * writing to become valid.
	 */
	if ((factors->offset == CLKSEL(9)) || (factors->offset == CLKSEL(10)))
		write_valid_reg = base + CLKSEL(11);
	else
		write_valid_reg = NULL;

	hw = m10v_clk_hw_register_divider(NULL, factors->name,
					  factors->parent_name,
					  CLK_SET_RATE_PARENT,
					  base + factors->offset,
					  factors->shift,
					  factors->width, factors->div_flags,
					  factors->table,
					  &m10v_crglock, write_valid_reg);

	if (factors->onecell_idx >= 0)
		clk_data->hws[factors->onecell_idx] = hw;
}

static void m10v_reg_fixed_pre(const struct m10v_clk_div_fixed_data *factors,
			       struct clk_hw_onecell_data *clk_data,
			       const char *parent_name)
{
	struct clk_hw *hw;
	const char *pn = factors->parent_name ?
				factors->parent_name : parent_name;

	hw = clk_hw_register_fixed_factor(NULL, factors->name, pn, 0,
					  factors->mult, factors->div);

	if (factors->onecell_idx >= 0)
		clk_data->hws[factors->onecell_idx] = hw;
}

static void m10v_reg_mux_pre(const struct m10v_clk_mux_factors *factors,
			       struct clk_hw_onecell_data *clk_data,
			       void __iomem *base)
{
	struct clk_hw *hw;

	hw = m10v_clk_hw_register_mux(NULL, factors->name,
				      factors->parent_names,
				      factors->num_parents,
				      CLK_SET_RATE_PARENT,
				      base + factors->offset, factors->shift,
				      factors->mask, factors->mux_flags,
				      factors->table, &m10v_crglock);

	if (factors->onecell_idx >= 0)
		clk_data->hws[factors->onecell_idx] = hw;
}

static int m10v_clk_probe(struct platform_device *pdev)
{
	int id;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;
	const char *parent_name;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	parent_name = of_clk_get_parent_name(np, 0);

	for (id = 0; id < ARRAY_SIZE(m10v_div_factor_data); ++id)
		m10v_reg_div_pre(&m10v_div_factor_data[id],
				 m10v_clk_data, base);

	for (id = 0; id < ARRAY_SIZE(m10v_div_fixed_data); ++id)
		m10v_reg_fixed_pre(&m10v_div_fixed_data[id],
				   m10v_clk_data, parent_name);

	for (id = 0; id < ARRAY_SIZE(m10v_mux_factor_data); ++id)
		m10v_reg_mux_pre(&m10v_mux_factor_data[id],
				 m10v_clk_data, base);

	for (id = 0; id < M10V_NUM_CLKS; id++) {
		if (IS_ERR(m10v_clk_data->hws[id]))
			return PTR_ERR(m10v_clk_data->hws[id]);
	}

	return 0;
}

static const struct of_device_id m10v_clk_dt_ids[] = {
	{ .compatible = "socionext,milbeaut-m10v-ccu", },
	{ }
};

static struct platform_driver m10v_clk_driver = {
	.probe  = m10v_clk_probe,
	.driver = {
		.name = "m10v-ccu",
		.of_match_table = m10v_clk_dt_ids,
	},
};
builtin_platform_driver(m10v_clk_driver);

static void __init m10v_cc_init(struct device_node *np)
{
	int id;
	void __iomem *base;
	const char *parent_name;
	struct clk_hw *hw;

	m10v_clk_data = kzalloc(struct_size(m10v_clk_data, hws,
					M10V_NUM_CLKS),
					GFP_KERNEL);

	if (!m10v_clk_data)
		return;

	base = of_iomap(np, 0);
	if (!base) {
		kfree(m10v_clk_data);
		return;
	}

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name) {
		kfree(m10v_clk_data);
		iounmap(base);
		return;
	}

	/*
	 * This way all clocks fetched before the platform device probes,
	 * except those we assign here for early use, will be deferred.
	 */
	for (id = 0; id < M10V_NUM_CLKS; id++)
		m10v_clk_data->hws[id] = ERR_PTR(-EPROBE_DEFER);

	/*
	 * PLLs are set by bootloader so this driver registers them as the
	 * fixed factor.
	 */
	for (id = 0; id < ARRAY_SIZE(m10v_pll_fixed_data); ++id)
		m10v_reg_fixed_pre(&m10v_pll_fixed_data[id],
				   m10v_clk_data, parent_name);

	/*
	 * timer consumes "rclk" so it needs to register here.
	 */
	hw = m10v_clk_hw_register_divider(NULL, "rclk", M10V_PLL10DIV2, 0,
					base + CLKSEL(1), 0, 3, 0, rclk_table,
					&m10v_crglock, NULL);
	m10v_clk_data->hws[M10V_RCLK_ID] = hw;

	m10v_clk_data->num = M10V_NUM_CLKS;
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, m10v_clk_data);
}
CLK_OF_DECLARE_DRIVER(m10v_cc, "socionext,milbeaut-m10v-ccu", m10v_cc_init);
