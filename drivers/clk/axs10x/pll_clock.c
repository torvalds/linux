// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys AXS10X SDP Generic PLL clock driver
 *
 * Copyright (C) 2017 Synopsys
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/of.h>

/* PLL registers addresses */
#define PLL_REG_IDIV	0x0
#define PLL_REG_FBDIV	0x4
#define PLL_REG_ODIV	0x8

/*
 * Bit fields of the PLL IDIV/FBDIV/ODIV registers:
 *  ________________________________________________________________________
 * |31                15|    14    |   13   |  12  |11         6|5         0|
 * |-------RESRVED------|-NOUPDATE-|-BYPASS-|-EDGE-|--HIGHTIME--|--LOWTIME--|
 * |____________________|__________|________|______|____________|___________|
 *
 * Following macros determine the way of access to these registers
 * They should be set up only using the macros.
 * reg should be an u32 variable.
 */

#define PLL_REG_GET_LOW(reg)			\
	(((reg) & (0x3F << 0)) >> 0)
#define PLL_REG_GET_HIGH(reg)			\
	(((reg) & (0x3F << 6)) >> 6)
#define PLL_REG_GET_EDGE(reg)			\
	(((reg) & (BIT(12))) ? 1 : 0)
#define PLL_REG_GET_BYPASS(reg)			\
	(((reg) & (BIT(13))) ? 1 : 0)
#define PLL_REG_GET_NOUPD(reg)			\
	(((reg) & (BIT(14))) ? 1 : 0)
#define PLL_REG_GET_PAD(reg)			\
	(((reg) & (0x1FFFF << 15)) >> 15)

#define PLL_REG_SET_LOW(reg, value)		\
	{ reg |= (((value) & 0x3F) << 0); }
#define PLL_REG_SET_HIGH(reg, value)		\
	{ reg |= (((value) & 0x3F) << 6); }
#define PLL_REG_SET_EDGE(reg, value)		\
	{ reg |= (((value) & 0x01) << 12); }
#define PLL_REG_SET_BYPASS(reg, value)		\
	{ reg |= (((value) & 0x01) << 13); }
#define PLL_REG_SET_NOUPD(reg, value)		\
	{ reg |= (((value) & 0x01) << 14); }
#define PLL_REG_SET_PAD(reg, value)		\
	{ reg |= (((value) & 0x1FFFF) << 15); }

#define PLL_LOCK	BIT(0)
#define PLL_ERROR	BIT(1)
#define PLL_MAX_LOCK_TIME 100 /* 100 us */

struct axs10x_pll_cfg {
	u32 rate;
	u32 idiv;
	u32 fbdiv;
	u32 odiv;
};

static const struct axs10x_pll_cfg arc_pll_cfg[] = {
	{ 33333333,  1, 1,  1 },
	{ 50000000,  1, 30, 20 },
	{ 75000000,  2, 45, 10 },
	{ 90000000,  2, 54, 10 },
	{ 100000000, 1, 30, 10 },
	{ 125000000, 2, 45, 6 },
	{}
};

static const struct axs10x_pll_cfg pgu_pll_cfg[] = {
	{ 25200000, 1, 84, 90 },
	{ 50000000, 1, 100, 54 },
	{ 74250000, 1, 44, 16 },
	{}
};

struct axs10x_pll_clk {
	struct clk_hw hw;
	void __iomem *base;
	void __iomem *lock;
	const struct axs10x_pll_cfg *pll_cfg;
	struct device *dev;
};

static inline void axs10x_pll_write(struct axs10x_pll_clk *clk, u32 reg,
				    u32 val)
{
	iowrite32(val, clk->base + reg);
}

static inline u32 axs10x_pll_read(struct axs10x_pll_clk *clk, u32 reg)
{
	return ioread32(clk->base + reg);
}

static inline struct axs10x_pll_clk *to_axs10x_pll_clk(struct clk_hw *hw)
{
	return container_of(hw, struct axs10x_pll_clk, hw);
}

static inline u32 axs10x_div_get_value(u32 reg)
{
	if (PLL_REG_GET_BYPASS(reg))
		return 1;

	return PLL_REG_GET_HIGH(reg) + PLL_REG_GET_LOW(reg);
}

static inline u32 axs10x_encode_div(unsigned int id, int upd)
{
	u32 div = 0;

	PLL_REG_SET_LOW(div, (id % 2 == 0) ? id >> 1 : (id >> 1) + 1);
	PLL_REG_SET_HIGH(div, id >> 1);
	PLL_REG_SET_EDGE(div, id % 2);
	PLL_REG_SET_BYPASS(div, id == 1 ? 1 : 0);
	PLL_REG_SET_NOUPD(div, upd == 0 ? 1 : 0);

	return div;
}

static unsigned long axs10x_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	u64 rate;
	u32 idiv, fbdiv, odiv;
	struct axs10x_pll_clk *clk = to_axs10x_pll_clk(hw);

	idiv = axs10x_div_get_value(axs10x_pll_read(clk, PLL_REG_IDIV));
	fbdiv = axs10x_div_get_value(axs10x_pll_read(clk, PLL_REG_FBDIV));
	odiv = axs10x_div_get_value(axs10x_pll_read(clk, PLL_REG_ODIV));

	rate = (u64)parent_rate * fbdiv;
	do_div(rate, idiv * odiv);

	return rate;
}

static long axs10x_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	int i;
	long best_rate;
	struct axs10x_pll_clk *clk = to_axs10x_pll_clk(hw);
	const struct axs10x_pll_cfg *pll_cfg = clk->pll_cfg;

	if (pll_cfg[0].rate == 0)
		return -EINVAL;

	best_rate = pll_cfg[0].rate;

	for (i = 1; pll_cfg[i].rate != 0; i++) {
		if (abs(rate - pll_cfg[i].rate) < abs(rate - best_rate))
			best_rate = pll_cfg[i].rate;
	}

	return best_rate;
}

static int axs10x_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	int i;
	struct axs10x_pll_clk *clk = to_axs10x_pll_clk(hw);
	const struct axs10x_pll_cfg *pll_cfg = clk->pll_cfg;

	for (i = 0; pll_cfg[i].rate != 0; i++) {
		if (pll_cfg[i].rate == rate) {
			axs10x_pll_write(clk, PLL_REG_IDIV,
					 axs10x_encode_div(pll_cfg[i].idiv, 0));
			axs10x_pll_write(clk, PLL_REG_FBDIV,
					 axs10x_encode_div(pll_cfg[i].fbdiv, 0));
			axs10x_pll_write(clk, PLL_REG_ODIV,
					 axs10x_encode_div(pll_cfg[i].odiv, 1));

			/*
			 * Wait until CGU relocks and check error status.
			 * If after timeout CGU is unlocked yet return error
			 */
			udelay(PLL_MAX_LOCK_TIME);
			if (!(ioread32(clk->lock) & PLL_LOCK))
				return -ETIMEDOUT;

			if (ioread32(clk->lock) & PLL_ERROR)
				return -EINVAL;

			return 0;
		}
	}

	dev_err(clk->dev, "invalid rate=%ld, parent_rate=%ld\n", rate,
			parent_rate);
	return -EINVAL;
}

static const struct clk_ops axs10x_pll_ops = {
	.recalc_rate = axs10x_pll_recalc_rate,
	.round_rate = axs10x_pll_round_rate,
	.set_rate = axs10x_pll_set_rate,
};

static int axs10x_pll_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *parent_name;
	struct axs10x_pll_clk *pll_clk;
	struct clk_init_data init = { };
	int ret;

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return -ENOMEM;

	pll_clk->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pll_clk->base))
		return PTR_ERR(pll_clk->base);

	pll_clk->lock = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pll_clk->lock))
		return PTR_ERR(pll_clk->lock);

	init.name = dev->of_node->name;
	init.ops = &axs10x_pll_ops;
	parent_name = of_clk_get_parent_name(dev->of_node, 0);
	init.parent_names = &parent_name;
	init.num_parents = 1;
	pll_clk->hw.init = &init;
	pll_clk->dev = dev;
	pll_clk->pll_cfg = of_device_get_match_data(dev);

	if (!pll_clk->pll_cfg) {
		dev_err(dev, "No OF match data provided\n");
		return -EINVAL;
	}

	ret = devm_clk_hw_register(dev, &pll_clk->hw);
	if (ret) {
		dev_err(dev, "failed to register %s clock\n", init.name);
		return ret;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &pll_clk->hw);
}

static void __init of_axs10x_pll_clk_setup(struct device_node *node)
{
	const char *parent_name;
	struct axs10x_pll_clk *pll_clk;
	struct clk_init_data init = { };
	int ret;

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return;

	pll_clk->base = of_iomap(node, 0);
	if (!pll_clk->base) {
		pr_err("failed to map pll div registers\n");
		goto err_free_pll_clk;
	}

	pll_clk->lock = of_iomap(node, 1);
	if (!pll_clk->lock) {
		pr_err("failed to map pll lock register\n");
		goto err_unmap_base;
	}

	init.name = node->name;
	init.ops = &axs10x_pll_ops;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	init.num_parents = parent_name ? 1 : 0;
	pll_clk->hw.init = &init;
	pll_clk->pll_cfg = arc_pll_cfg;

	ret = clk_hw_register(NULL, &pll_clk->hw);
	if (ret) {
		pr_err("failed to register %pOFn clock\n", node);
		goto err_unmap_lock;
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get, &pll_clk->hw);
	if (ret) {
		pr_err("failed to add hw provider for %pOFn clock\n", node);
		goto err_unregister_clk;
	}

	return;

err_unregister_clk:
	clk_hw_unregister(&pll_clk->hw);
err_unmap_lock:
	iounmap(pll_clk->lock);
err_unmap_base:
	iounmap(pll_clk->base);
err_free_pll_clk:
	kfree(pll_clk);
}
CLK_OF_DECLARE(axs10x_pll_clock, "snps,axs10x-arc-pll-clock",
	       of_axs10x_pll_clk_setup);

static const struct of_device_id axs10x_pll_clk_id[] = {
	{ .compatible = "snps,axs10x-pgu-pll-clock", .data = &pgu_pll_cfg},
	{ }
};
MODULE_DEVICE_TABLE(of, axs10x_pll_clk_id);

static struct platform_driver axs10x_pll_clk_driver = {
	.driver = {
		.name = "axs10x-pll-clock",
		.of_match_table = axs10x_pll_clk_id,
	},
	.probe = axs10x_pll_clk_probe,
};
builtin_platform_driver(axs10x_pll_clk_driver);

MODULE_AUTHOR("Vlad Zakharov <vzakhar@synopsys.com>");
MODULE_DESCRIPTION("Synopsys AXS10X SDP Generic PLL Clock Driver");
MODULE_LICENSE("GPL v2");
