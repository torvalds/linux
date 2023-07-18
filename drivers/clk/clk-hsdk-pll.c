// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synopsys HSDK SDP Generic PLL clock driver
 *
 * Copyright (C) 2017 Synopsys
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define CGU_PLL_CTRL	0x000 /* ARC PLL control register */
#define CGU_PLL_STATUS	0x004 /* ARC PLL status register */
#define CGU_PLL_FMEAS	0x008 /* ARC PLL frequency measurement register */
#define CGU_PLL_MON	0x00C /* ARC PLL monitor register */

#define CGU_PLL_CTRL_ODIV_SHIFT		2
#define CGU_PLL_CTRL_IDIV_SHIFT		4
#define CGU_PLL_CTRL_FBDIV_SHIFT	9
#define CGU_PLL_CTRL_BAND_SHIFT		20

#define CGU_PLL_CTRL_ODIV_MASK		GENMASK(3, CGU_PLL_CTRL_ODIV_SHIFT)
#define CGU_PLL_CTRL_IDIV_MASK		GENMASK(8, CGU_PLL_CTRL_IDIV_SHIFT)
#define CGU_PLL_CTRL_FBDIV_MASK		GENMASK(15, CGU_PLL_CTRL_FBDIV_SHIFT)

#define CGU_PLL_CTRL_PD			BIT(0)
#define CGU_PLL_CTRL_BYPASS		BIT(1)

#define CGU_PLL_STATUS_LOCK		BIT(0)
#define CGU_PLL_STATUS_ERR		BIT(1)

#define HSDK_PLL_MAX_LOCK_TIME		100 /* 100 us */

#define CGU_PLL_SOURCE_MAX		1

#define CORE_IF_CLK_THRESHOLD_HZ	500000000
#define CREG_CORE_IF_CLK_DIV_1		0x0
#define CREG_CORE_IF_CLK_DIV_2		0x1

struct hsdk_pll_cfg {
	u32 rate;
	u32 idiv;
	u32 fbdiv;
	u32 odiv;
	u32 band;
	u32 bypass;
};

static const struct hsdk_pll_cfg asdt_pll_cfg[] = {
	{ 100000000,  0, 11, 3, 0, 0 },
	{ 133000000,  0, 15, 3, 0, 0 },
	{ 200000000,  1, 47, 3, 0, 0 },
	{ 233000000,  1, 27, 2, 0, 0 },
	{ 300000000,  1, 35, 2, 0, 0 },
	{ 333000000,  1, 39, 2, 0, 0 },
	{ 400000000,  1, 47, 2, 0, 0 },
	{ 500000000,  0, 14, 1, 0, 0 },
	{ 600000000,  0, 17, 1, 0, 0 },
	{ 700000000,  0, 20, 1, 0, 0 },
	{ 800000000,  0, 23, 1, 0, 0 },
	{ 900000000,  1, 26, 0, 0, 0 },
	{ 1000000000, 1, 29, 0, 0, 0 },
	{ 1100000000, 1, 32, 0, 0, 0 },
	{ 1200000000, 1, 35, 0, 0, 0 },
	{ 1300000000, 1, 38, 0, 0, 0 },
	{ 1400000000, 1, 41, 0, 0, 0 },
	{ 1500000000, 1, 44, 0, 0, 0 },
	{ 1600000000, 1, 47, 0, 0, 0 },
	{}
};

static const struct hsdk_pll_cfg hdmi_pll_cfg[] = {
	{ 27000000,   0, 0,  0, 0, 1 },
	{ 148500000,  0, 21, 3, 0, 0 },
	{ 297000000,  0, 21, 2, 0, 0 },
	{ 540000000,  0, 19, 1, 0, 0 },
	{ 594000000,  0, 21, 1, 0, 0 },
	{}
};

struct hsdk_pll_clk {
	struct clk_hw hw;
	void __iomem *regs;
	void __iomem *spec_regs;
	const struct hsdk_pll_devdata *pll_devdata;
	struct device *dev;
};

struct hsdk_pll_devdata {
	const struct hsdk_pll_cfg *pll_cfg;
	int (*update_rate)(struct hsdk_pll_clk *clk, unsigned long rate,
			   const struct hsdk_pll_cfg *cfg);
};

static int hsdk_pll_core_update_rate(struct hsdk_pll_clk *, unsigned long,
				     const struct hsdk_pll_cfg *);
static int hsdk_pll_comm_update_rate(struct hsdk_pll_clk *, unsigned long,
				     const struct hsdk_pll_cfg *);

static const struct hsdk_pll_devdata core_pll_devdata = {
	.pll_cfg = asdt_pll_cfg,
	.update_rate = hsdk_pll_core_update_rate,
};

static const struct hsdk_pll_devdata sdt_pll_devdata = {
	.pll_cfg = asdt_pll_cfg,
	.update_rate = hsdk_pll_comm_update_rate,
};

static const struct hsdk_pll_devdata hdmi_pll_devdata = {
	.pll_cfg = hdmi_pll_cfg,
	.update_rate = hsdk_pll_comm_update_rate,
};

static inline void hsdk_pll_write(struct hsdk_pll_clk *clk, u32 reg, u32 val)
{
	iowrite32(val, clk->regs + reg);
}

static inline u32 hsdk_pll_read(struct hsdk_pll_clk *clk, u32 reg)
{
	return ioread32(clk->regs + reg);
}

static inline void hsdk_pll_set_cfg(struct hsdk_pll_clk *clk,
				    const struct hsdk_pll_cfg *cfg)
{
	u32 val = 0;

	if (cfg->bypass) {
		val = hsdk_pll_read(clk, CGU_PLL_CTRL);
		val |= CGU_PLL_CTRL_BYPASS;
	} else {
		/* Powerdown and Bypass bits should be cleared */
		val |= cfg->idiv << CGU_PLL_CTRL_IDIV_SHIFT;
		val |= cfg->fbdiv << CGU_PLL_CTRL_FBDIV_SHIFT;
		val |= cfg->odiv << CGU_PLL_CTRL_ODIV_SHIFT;
		val |= cfg->band << CGU_PLL_CTRL_BAND_SHIFT;
	}

	dev_dbg(clk->dev, "write configuration: %#x\n", val);

	hsdk_pll_write(clk, CGU_PLL_CTRL, val);
}

static inline bool hsdk_pll_is_locked(struct hsdk_pll_clk *clk)
{
	return !!(hsdk_pll_read(clk, CGU_PLL_STATUS) & CGU_PLL_STATUS_LOCK);
}

static inline bool hsdk_pll_is_err(struct hsdk_pll_clk *clk)
{
	return !!(hsdk_pll_read(clk, CGU_PLL_STATUS) & CGU_PLL_STATUS_ERR);
}

static inline struct hsdk_pll_clk *to_hsdk_pll_clk(struct clk_hw *hw)
{
	return container_of(hw, struct hsdk_pll_clk, hw);
}

static unsigned long hsdk_pll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	u32 val;
	u64 rate;
	u32 idiv, fbdiv, odiv;
	struct hsdk_pll_clk *clk = to_hsdk_pll_clk(hw);

	val = hsdk_pll_read(clk, CGU_PLL_CTRL);

	dev_dbg(clk->dev, "current configuration: %#x\n", val);

	/* Check if PLL is bypassed */
	if (val & CGU_PLL_CTRL_BYPASS)
		return parent_rate;

	/* Check if PLL is disabled */
	if (val & CGU_PLL_CTRL_PD)
		return 0;

	/* input divider = reg.idiv + 1 */
	idiv = 1 + ((val & CGU_PLL_CTRL_IDIV_MASK) >> CGU_PLL_CTRL_IDIV_SHIFT);
	/* fb divider = 2*(reg.fbdiv + 1) */
	fbdiv = 2 * (1 + ((val & CGU_PLL_CTRL_FBDIV_MASK) >> CGU_PLL_CTRL_FBDIV_SHIFT));
	/* output divider = 2^(reg.odiv) */
	odiv = 1 << ((val & CGU_PLL_CTRL_ODIV_MASK) >> CGU_PLL_CTRL_ODIV_SHIFT);

	rate = (u64)parent_rate * fbdiv;
	do_div(rate, idiv * odiv);

	return rate;
}

static long hsdk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	int i;
	unsigned long best_rate;
	struct hsdk_pll_clk *clk = to_hsdk_pll_clk(hw);
	const struct hsdk_pll_cfg *pll_cfg = clk->pll_devdata->pll_cfg;

	if (pll_cfg[0].rate == 0)
		return -EINVAL;

	best_rate = pll_cfg[0].rate;

	for (i = 1; pll_cfg[i].rate != 0; i++) {
		if (abs(rate - pll_cfg[i].rate) < abs(rate - best_rate))
			best_rate = pll_cfg[i].rate;
	}

	dev_dbg(clk->dev, "chosen best rate: %lu\n", best_rate);

	return best_rate;
}

static int hsdk_pll_comm_update_rate(struct hsdk_pll_clk *clk,
				     unsigned long rate,
				     const struct hsdk_pll_cfg *cfg)
{
	hsdk_pll_set_cfg(clk, cfg);

	/*
	 * Wait until CGU relocks and check error status.
	 * If after timeout CGU is unlocked yet return error.
	 */
	udelay(HSDK_PLL_MAX_LOCK_TIME);
	if (!hsdk_pll_is_locked(clk))
		return -ETIMEDOUT;

	if (hsdk_pll_is_err(clk))
		return -EINVAL;

	return 0;
}

static int hsdk_pll_core_update_rate(struct hsdk_pll_clk *clk,
				     unsigned long rate,
				     const struct hsdk_pll_cfg *cfg)
{
	/*
	 * When core clock exceeds 500MHz, the divider for the interface
	 * clock must be programmed to div-by-2.
	 */
	if (rate > CORE_IF_CLK_THRESHOLD_HZ)
		iowrite32(CREG_CORE_IF_CLK_DIV_2, clk->spec_regs);

	hsdk_pll_set_cfg(clk, cfg);

	/*
	 * Wait until CGU relocks and check error status.
	 * If after timeout CGU is unlocked yet return error.
	 */
	udelay(HSDK_PLL_MAX_LOCK_TIME);
	if (!hsdk_pll_is_locked(clk))
		return -ETIMEDOUT;

	if (hsdk_pll_is_err(clk))
		return -EINVAL;

	/*
	 * Program divider to div-by-1 if we succesfuly set core clock below
	 * 500MHz threshold.
	 */
	if (rate <= CORE_IF_CLK_THRESHOLD_HZ)
		iowrite32(CREG_CORE_IF_CLK_DIV_1, clk->spec_regs);

	return 0;
}

static int hsdk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	int i;
	struct hsdk_pll_clk *clk = to_hsdk_pll_clk(hw);
	const struct hsdk_pll_cfg *pll_cfg = clk->pll_devdata->pll_cfg;

	for (i = 0; pll_cfg[i].rate != 0; i++) {
		if (pll_cfg[i].rate == rate) {
			return clk->pll_devdata->update_rate(clk, rate,
							     &pll_cfg[i]);
		}
	}

	dev_err(clk->dev, "invalid rate=%ld, parent_rate=%ld\n", rate,
			parent_rate);

	return -EINVAL;
}

static const struct clk_ops hsdk_pll_ops = {
	.recalc_rate = hsdk_pll_recalc_rate,
	.round_rate = hsdk_pll_round_rate,
	.set_rate = hsdk_pll_set_rate,
};

static int hsdk_pll_clk_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *mem;
	const char *parent_name;
	unsigned int num_parents;
	struct hsdk_pll_clk *pll_clk;
	struct clk_init_data init = { };
	struct device *dev = &pdev->dev;

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return -ENOMEM;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pll_clk->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(pll_clk->regs))
		return PTR_ERR(pll_clk->regs);

	init.name = dev->of_node->name;
	init.ops = &hsdk_pll_ops;
	parent_name = of_clk_get_parent_name(dev->of_node, 0);
	init.parent_names = &parent_name;
	num_parents = of_clk_get_parent_count(dev->of_node);
	if (num_parents == 0 || num_parents > CGU_PLL_SOURCE_MAX) {
		dev_err(dev, "wrong clock parents number: %u\n", num_parents);
		return -EINVAL;
	}
	init.num_parents = num_parents;

	pll_clk->hw.init = &init;
	pll_clk->dev = dev;
	pll_clk->pll_devdata = of_device_get_match_data(dev);

	if (!pll_clk->pll_devdata) {
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

static void __init of_hsdk_pll_clk_setup(struct device_node *node)
{
	int ret;
	const char *parent_name;
	unsigned int num_parents;
	struct hsdk_pll_clk *pll_clk;
	struct clk_init_data init = { };

	pll_clk = kzalloc(sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return;

	pll_clk->regs = of_iomap(node, 0);
	if (!pll_clk->regs) {
		pr_err("failed to map pll registers\n");
		goto err_free_pll_clk;
	}

	pll_clk->spec_regs = of_iomap(node, 1);
	if (!pll_clk->spec_regs) {
		pr_err("failed to map pll registers\n");
		goto err_unmap_comm_regs;
	}

	init.name = node->name;
	init.ops = &hsdk_pll_ops;
	parent_name = of_clk_get_parent_name(node, 0);
	init.parent_names = &parent_name;
	num_parents = of_clk_get_parent_count(node);
	if (num_parents > CGU_PLL_SOURCE_MAX) {
		pr_err("too much clock parents: %u\n", num_parents);
		goto err_unmap_spec_regs;
	}
	init.num_parents = num_parents;

	pll_clk->hw.init = &init;
	pll_clk->pll_devdata = &core_pll_devdata;

	ret = clk_hw_register(NULL, &pll_clk->hw);
	if (ret) {
		pr_err("failed to register %pOFn clock\n", node);
		goto err_unmap_spec_regs;
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get, &pll_clk->hw);
	if (ret) {
		pr_err("failed to add hw provider for %pOFn clock\n", node);
		goto err_unmap_spec_regs;
	}

	return;

err_unmap_spec_regs:
	iounmap(pll_clk->spec_regs);
err_unmap_comm_regs:
	iounmap(pll_clk->regs);
err_free_pll_clk:
	kfree(pll_clk);
}

/* Core PLL needed early for ARC cpus timers */
CLK_OF_DECLARE(hsdk_pll_clock, "snps,hsdk-core-pll-clock",
of_hsdk_pll_clk_setup);

static const struct of_device_id hsdk_pll_clk_id[] = {
	{ .compatible = "snps,hsdk-gp-pll-clock", .data = &sdt_pll_devdata},
	{ .compatible = "snps,hsdk-hdmi-pll-clock", .data = &hdmi_pll_devdata},
	{ }
};

static struct platform_driver hsdk_pll_clk_driver = {
	.driver = {
		.name = "hsdk-gp-pll-clock",
		.of_match_table = hsdk_pll_clk_id,
	},
	.probe = hsdk_pll_clk_probe,
};
builtin_platform_driver(hsdk_pll_clk_driver);
