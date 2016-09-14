/*
 * Synopsys AXS10X SDP I2S PLL clock driver
 *
 * Copyright (C) 2016 Synopsys
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/of.h>

/* PLL registers addresses */
#define PLL_IDIV_REG	0x0
#define PLL_FBDIV_REG	0x4
#define PLL_ODIV0_REG	0x8
#define PLL_ODIV1_REG	0xC

struct i2s_pll_cfg {
	unsigned int rate;
	unsigned int idiv;
	unsigned int fbdiv;
	unsigned int odiv0;
	unsigned int odiv1;
};

static const struct i2s_pll_cfg i2s_pll_cfg_27m[] = {
	/* 27 Mhz */
	{ 1024000, 0x104, 0x451, 0x10E38, 0x2000 },
	{ 1411200, 0x104, 0x596, 0x10D35, 0x2000 },
	{ 1536000, 0x208, 0xA28, 0x10B2C, 0x2000 },
	{ 2048000, 0x82, 0x451, 0x10E38, 0x2000 },
	{ 2822400, 0x82, 0x596, 0x10D35, 0x2000 },
	{ 3072000, 0x104, 0xA28, 0x10B2C, 0x2000 },
	{ 2116800, 0x82, 0x3CF, 0x10C30, 0x2000 },
	{ 2304000, 0x104, 0x79E, 0x10B2C, 0x2000 },
	{ 0, 0, 0, 0, 0 },
};

static const struct i2s_pll_cfg i2s_pll_cfg_28m[] = {
	/* 28.224 Mhz */
	{ 1024000, 0x82, 0x105, 0x107DF, 0x2000 },
	{ 1411200, 0x28A, 0x1, 0x10001, 0x2000 },
	{ 1536000, 0xA28, 0x187, 0x10042, 0x2000 },
	{ 2048000, 0x41, 0x105, 0x107DF, 0x2000 },
	{ 2822400, 0x145, 0x1, 0x10001, 0x2000 },
	{ 3072000, 0x514, 0x187, 0x10042, 0x2000 },
	{ 2116800, 0x514, 0x42, 0x10001, 0x2000 },
	{ 2304000, 0x619, 0x82, 0x10001, 0x2000 },
	{ 0, 0, 0, 0, 0 },
};

struct i2s_pll_clk {
	void __iomem *base;
	struct clk_hw hw;
	struct device *dev;
};

static inline void i2s_pll_write(struct i2s_pll_clk *clk, unsigned int reg,
		unsigned int val)
{
	writel_relaxed(val, clk->base + reg);
}

static inline unsigned int i2s_pll_read(struct i2s_pll_clk *clk,
		unsigned int reg)
{
	return readl_relaxed(clk->base + reg);
}

static inline struct i2s_pll_clk *to_i2s_pll_clk(struct clk_hw *hw)
{
	return container_of(hw, struct i2s_pll_clk, hw);
}

static inline unsigned int i2s_pll_get_value(unsigned int val)
{
	return (val & 0x3F) + ((val >> 6) & 0x3F);
}

static const struct i2s_pll_cfg *i2s_pll_get_cfg(unsigned long prate)
{
	switch (prate) {
	case 27000000:
		return i2s_pll_cfg_27m;
	case 28224000:
		return i2s_pll_cfg_28m;
	default:
		return NULL;
	}
}

static unsigned long i2s_pll_recalc_rate(struct clk_hw *hw,
			unsigned long parent_rate)
{
	struct i2s_pll_clk *clk = to_i2s_pll_clk(hw);
	unsigned int idiv, fbdiv, odiv;

	idiv = i2s_pll_get_value(i2s_pll_read(clk, PLL_IDIV_REG));
	fbdiv = i2s_pll_get_value(i2s_pll_read(clk, PLL_FBDIV_REG));
	odiv = i2s_pll_get_value(i2s_pll_read(clk, PLL_ODIV0_REG));

	return ((parent_rate / idiv) * fbdiv) / odiv;
}

static long i2s_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct i2s_pll_clk *clk = to_i2s_pll_clk(hw);
	const struct i2s_pll_cfg *pll_cfg = i2s_pll_get_cfg(*prate);
	int i;

	if (!pll_cfg) {
		dev_err(clk->dev, "invalid parent rate=%ld\n", *prate);
		return -EINVAL;
	}

	for (i = 0; pll_cfg[i].rate != 0; i++)
		if (pll_cfg[i].rate == rate)
			return rate;

	return -EINVAL;
}

static int i2s_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct i2s_pll_clk *clk = to_i2s_pll_clk(hw);
	const struct i2s_pll_cfg *pll_cfg = i2s_pll_get_cfg(parent_rate);
	int i;

	if (!pll_cfg) {
		dev_err(clk->dev, "invalid parent rate=%ld\n", parent_rate);
		return -EINVAL;
	}

	for (i = 0; pll_cfg[i].rate != 0; i++) {
		if (pll_cfg[i].rate == rate) {
			i2s_pll_write(clk, PLL_IDIV_REG, pll_cfg[i].idiv);
			i2s_pll_write(clk, PLL_FBDIV_REG, pll_cfg[i].fbdiv);
			i2s_pll_write(clk, PLL_ODIV0_REG, pll_cfg[i].odiv0);
			i2s_pll_write(clk, PLL_ODIV1_REG, pll_cfg[i].odiv1);
			return 0;
		}
	}

	dev_err(clk->dev, "invalid rate=%ld, parent_rate=%ld\n", rate,
			parent_rate);
	return -EINVAL;
}

static const struct clk_ops i2s_pll_ops = {
	.recalc_rate = i2s_pll_recalc_rate,
	.round_rate = i2s_pll_round_rate,
	.set_rate = i2s_pll_set_rate,
};

static int i2s_pll_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	const char *clk_name;
	const char *parent_name;
	struct clk *clk;
	struct i2s_pll_clk *pll_clk;
	struct clk_init_data init;
	struct resource *mem;

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pll_clk->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(pll_clk->base))
		return PTR_ERR(pll_clk->base);

	clk_name = node->name;
	init.name = clk_name;
	init.ops = &i2s_pll_ops;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;
	pll_clk->hw.init = &init;
	pll_clk->dev = dev;

	clk = devm_clk_register(dev, &pll_clk->hw);
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to register %s clock (%ld)\n",
				clk_name, PTR_ERR(clk));
		return PTR_ERR(clk);
	}

	return of_clk_add_provider(node, of_clk_src_simple_get, clk);
}

static int i2s_pll_clk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);
	return 0;
}

static const struct of_device_id i2s_pll_clk_id[] = {
	{ .compatible = "snps,axs10x-i2s-pll-clock", },
	{ },
};
MODULE_DEVICE_TABLE(of, i2s_pll_clk_id);

static struct platform_driver i2s_pll_clk_driver = {
	.driver = {
		.name = "axs10x-i2s-pll-clock",
		.of_match_table = i2s_pll_clk_id,
	},
	.probe = i2s_pll_clk_probe,
	.remove = i2s_pll_clk_remove,
};
module_platform_driver(i2s_pll_clk_driver);

MODULE_AUTHOR("Jose Abreu <joabreu@synopsys.com>");
MODULE_DESCRIPTION("Synopsys AXS10X SDP I2S PLL Clock Driver");
MODULE_LICENSE("GPL v2");
