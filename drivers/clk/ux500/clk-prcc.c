/*
 * PRCC clock implementation for ux500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk-provider.h>
#include <linux/clk-private.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/types.h>

#include "clk.h"

#define PRCC_PCKEN			0x000
#define PRCC_PCKDIS			0x004
#define PRCC_KCKEN			0x008
#define PRCC_KCKDIS			0x00C
#define PRCC_PCKSR			0x010
#define PRCC_KCKSR			0x014

#define to_clk_prcc(_hw) container_of(_hw, struct clk_prcc, hw)

struct clk_prcc {
	struct clk_hw hw;
	void __iomem *base;
	u32 cg_sel;
	int is_enabled;
};

/* PRCC clock operations. */

static int clk_prcc_pclk_enable(struct clk_hw *hw)
{
	struct clk_prcc *clk = to_clk_prcc(hw);

	writel(clk->cg_sel, (clk->base + PRCC_PCKEN));
	while (!(readl(clk->base + PRCC_PCKSR) & clk->cg_sel))
		cpu_relax();

	clk->is_enabled = 1;
	return 0;
}

static void clk_prcc_pclk_disable(struct clk_hw *hw)
{
	struct clk_prcc *clk = to_clk_prcc(hw);

	writel(clk->cg_sel, (clk->base + PRCC_PCKDIS));
	clk->is_enabled = 0;
}

static int clk_prcc_kclk_enable(struct clk_hw *hw)
{
	struct clk_prcc *clk = to_clk_prcc(hw);

	writel(clk->cg_sel, (clk->base + PRCC_KCKEN));
	while (!(readl(clk->base + PRCC_KCKSR) & clk->cg_sel))
		cpu_relax();

	clk->is_enabled = 1;
	return 0;
}

static void clk_prcc_kclk_disable(struct clk_hw *hw)
{
	struct clk_prcc *clk = to_clk_prcc(hw);

	writel(clk->cg_sel, (clk->base + PRCC_KCKDIS));
	clk->is_enabled = 0;
}

static int clk_prcc_is_enabled(struct clk_hw *hw)
{
	struct clk_prcc *clk = to_clk_prcc(hw);
	return clk->is_enabled;
}

static struct clk_ops clk_prcc_pclk_ops = {
	.enable = clk_prcc_pclk_enable,
	.disable = clk_prcc_pclk_disable,
	.is_enabled = clk_prcc_is_enabled,
};

static struct clk_ops clk_prcc_kclk_ops = {
	.enable = clk_prcc_kclk_enable,
	.disable = clk_prcc_kclk_disable,
	.is_enabled = clk_prcc_is_enabled,
};

static struct clk *clk_reg_prcc(const char *name,
				const char *parent_name,
				resource_size_t phy_base,
				u32 cg_sel,
				unsigned long flags,
				struct clk_ops *clk_prcc_ops)
{
	struct clk_prcc *clk;
	struct clk_init_data clk_prcc_init;
	struct clk *clk_reg;

	if (!name) {
		pr_err("clk_prcc: %s invalid arguments passed\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	clk = kzalloc(sizeof(struct clk_prcc), GFP_KERNEL);
	if (!clk) {
		pr_err("clk_prcc: %s could not allocate clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	clk->base = ioremap(phy_base, SZ_4K);
	if (!clk->base)
		goto free_clk;

	clk->cg_sel = cg_sel;
	clk->is_enabled = 1;

	clk_prcc_init.name = name;
	clk_prcc_init.ops = clk_prcc_ops;
	clk_prcc_init.flags = flags;
	clk_prcc_init.parent_names = (parent_name ? &parent_name : NULL);
	clk_prcc_init.num_parents = (parent_name ? 1 : 0);
	clk->hw.init = &clk_prcc_init;

	clk_reg = clk_register(NULL, &clk->hw);
	if (IS_ERR_OR_NULL(clk_reg))
		goto unmap_clk;

	return clk_reg;

unmap_clk:
	iounmap(clk->base);
free_clk:
	kfree(clk);
	pr_err("clk_prcc: %s failed to register clk\n", __func__);
	return ERR_PTR(-ENOMEM);
}

struct clk *clk_reg_prcc_pclk(const char *name,
			      const char *parent_name,
			      resource_size_t phy_base,
			      u32 cg_sel,
			      unsigned long flags)
{
	return clk_reg_prcc(name, parent_name, phy_base, cg_sel, flags,
			&clk_prcc_pclk_ops);
}

struct clk *clk_reg_prcc_kclk(const char *name,
			      const char *parent_name,
			      resource_size_t phy_base,
			      u32 cg_sel,
			      unsigned long flags)
{
	return clk_reg_prcc(name, parent_name, phy_base, cg_sel, flags,
			&clk_prcc_kclk_ops);
}
