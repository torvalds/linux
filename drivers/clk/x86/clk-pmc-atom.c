/*
 * Intel Atom platform clocks driver for BayTrail and CherryTrail SoCs
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Irina Tirdea <irina.tirdea@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/platform_data/x86/clk-pmc-atom.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define PLT_CLK_NAME_BASE	"pmc_plt_clk"

#define PMC_CLK_CTL_OFFSET		0x60
#define PMC_CLK_CTL_SIZE		4
#define PMC_CLK_NUM			6
#define PMC_CLK_CTL_GATED_ON_D3		0x0
#define PMC_CLK_CTL_FORCE_ON		0x1
#define PMC_CLK_CTL_FORCE_OFF		0x2
#define PMC_CLK_CTL_RESERVED		0x3
#define PMC_MASK_CLK_CTL		GENMASK(1, 0)
#define PMC_MASK_CLK_FREQ		BIT(2)
#define PMC_CLK_FREQ_XTAL		(0 << 2)	/* 25 MHz */
#define PMC_CLK_FREQ_PLL		(1 << 2)	/* 19.2 MHz */

struct clk_plt_fixed {
	struct clk_hw *clk;
	struct clk_lookup *lookup;
};

struct clk_plt {
	struct clk_hw hw;
	void __iomem *reg;
	struct clk_lookup *lookup;
	/* protect access to PMC registers */
	spinlock_t lock;
};

#define to_clk_plt(_hw) container_of(_hw, struct clk_plt, hw)

struct clk_plt_data {
	struct clk_plt_fixed **parents;
	u8 nparents;
	struct clk_plt *clks[PMC_CLK_NUM];
	struct clk_lookup *mclk_lookup;
};

/* Return an index in parent table */
static inline int plt_reg_to_parent(int reg)
{
	switch (reg & PMC_MASK_CLK_FREQ) {
	default:
	case PMC_CLK_FREQ_XTAL:
		return 0;
	case PMC_CLK_FREQ_PLL:
		return 1;
	}
}

/* Return clk index of parent */
static inline int plt_parent_to_reg(int index)
{
	switch (index) {
	default:
	case 0:
		return PMC_CLK_FREQ_XTAL;
	case 1:
		return PMC_CLK_FREQ_PLL;
	}
}

/* Abstract status in simpler enabled/disabled value */
static inline int plt_reg_to_enabled(int reg)
{
	switch (reg & PMC_MASK_CLK_CTL) {
	case PMC_CLK_CTL_GATED_ON_D3:
	case PMC_CLK_CTL_FORCE_ON:
		return 1;	/* enabled */
	case PMC_CLK_CTL_FORCE_OFF:
	case PMC_CLK_CTL_RESERVED:
	default:
		return 0;	/* disabled */
	}
}

static void plt_clk_reg_update(struct clk_plt *clk, u32 mask, u32 val)
{
	u32 tmp;
	unsigned long flags;

	spin_lock_irqsave(&clk->lock, flags);

	tmp = readl(clk->reg);
	tmp = (tmp & ~mask) | (val & mask);
	writel(tmp, clk->reg);

	spin_unlock_irqrestore(&clk->lock, flags);
}

static int plt_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_plt *clk = to_clk_plt(hw);

	plt_clk_reg_update(clk, PMC_MASK_CLK_FREQ, plt_parent_to_reg(index));

	return 0;
}

static u8 plt_clk_get_parent(struct clk_hw *hw)
{
	struct clk_plt *clk = to_clk_plt(hw);
	u32 value;

	value = readl(clk->reg);

	return plt_reg_to_parent(value);
}

static int plt_clk_enable(struct clk_hw *hw)
{
	struct clk_plt *clk = to_clk_plt(hw);

	plt_clk_reg_update(clk, PMC_MASK_CLK_CTL, PMC_CLK_CTL_FORCE_ON);

	return 0;
}

static void plt_clk_disable(struct clk_hw *hw)
{
	struct clk_plt *clk = to_clk_plt(hw);

	plt_clk_reg_update(clk, PMC_MASK_CLK_CTL, PMC_CLK_CTL_FORCE_OFF);
}

static int plt_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_plt *clk = to_clk_plt(hw);
	u32 value;

	value = readl(clk->reg);

	return plt_reg_to_enabled(value);
}

static const struct clk_ops plt_clk_ops = {
	.enable = plt_clk_enable,
	.disable = plt_clk_disable,
	.is_enabled = plt_clk_is_enabled,
	.get_parent = plt_clk_get_parent,
	.set_parent = plt_clk_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static struct clk_plt *plt_clk_register(struct platform_device *pdev, int id,
					void __iomem *base,
					const char **parent_names,
					int num_parents)
{
	struct clk_plt *pclk;
	struct clk_init_data init;
	int ret;

	pclk = devm_kzalloc(&pdev->dev, sizeof(*pclk), GFP_KERNEL);
	if (!pclk)
		return ERR_PTR(-ENOMEM);

	init.name =  kasprintf(GFP_KERNEL, "%s_%d", PLT_CLK_NAME_BASE, id);
	init.ops = &plt_clk_ops;
	init.flags = 0;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	pclk->hw.init = &init;
	pclk->reg = base + PMC_CLK_CTL_OFFSET + id * PMC_CLK_CTL_SIZE;
	spin_lock_init(&pclk->lock);

	ret = devm_clk_hw_register(&pdev->dev, &pclk->hw);
	if (ret) {
		pclk = ERR_PTR(ret);
		goto err_free_init;
	}

	pclk->lookup = clkdev_hw_create(&pclk->hw, init.name, NULL);
	if (!pclk->lookup) {
		pclk = ERR_PTR(-ENOMEM);
		goto err_free_init;
	}

err_free_init:
	kfree(init.name);
	return pclk;
}

static void plt_clk_unregister(struct clk_plt *pclk)
{
	clkdev_drop(pclk->lookup);
}

static struct clk_plt_fixed *plt_clk_register_fixed_rate(struct platform_device *pdev,
						 const char *name,
						 const char *parent_name,
						 unsigned long fixed_rate)
{
	struct clk_plt_fixed *pclk;

	pclk = devm_kzalloc(&pdev->dev, sizeof(*pclk), GFP_KERNEL);
	if (!pclk)
		return ERR_PTR(-ENOMEM);

	pclk->clk = clk_hw_register_fixed_rate(&pdev->dev, name, parent_name,
					       0, fixed_rate);
	if (IS_ERR(pclk->clk))
		return ERR_CAST(pclk->clk);

	pclk->lookup = clkdev_hw_create(pclk->clk, name, NULL);
	if (!pclk->lookup) {
		clk_hw_unregister_fixed_rate(pclk->clk);
		return ERR_PTR(-ENOMEM);
	}

	return pclk;
}

static void plt_clk_unregister_fixed_rate(struct clk_plt_fixed *pclk)
{
	clkdev_drop(pclk->lookup);
	clk_hw_unregister_fixed_rate(pclk->clk);
}

static void plt_clk_unregister_fixed_rate_loop(struct clk_plt_data *data,
					       unsigned int i)
{
	while (i--)
		plt_clk_unregister_fixed_rate(data->parents[i]);
}

static void plt_clk_free_parent_names_loop(const char **parent_names,
					   unsigned int i)
{
	while (i--)
		kfree_const(parent_names[i]);
	kfree(parent_names);
}

static void plt_clk_unregister_loop(struct clk_plt_data *data,
				    unsigned int i)
{
	while (i--)
		plt_clk_unregister(data->clks[i]);
}

static const char **plt_clk_register_parents(struct platform_device *pdev,
					     struct clk_plt_data *data,
					     const struct pmc_clk *clks)
{
	const char **parent_names;
	unsigned int i;
	int err;
	int nparents = 0;

	data->nparents = 0;
	while (clks[nparents].name)
		nparents++;

	data->parents = devm_kcalloc(&pdev->dev, nparents,
				     sizeof(*data->parents), GFP_KERNEL);
	if (!data->parents)
		return ERR_PTR(-ENOMEM);

	parent_names = kcalloc(nparents, sizeof(*parent_names),
			       GFP_KERNEL);
	if (!parent_names)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nparents; i++) {
		data->parents[i] =
			plt_clk_register_fixed_rate(pdev, clks[i].name,
						    clks[i].parent_name,
						    clks[i].freq);
		if (IS_ERR(data->parents[i])) {
			err = PTR_ERR(data->parents[i]);
			goto err_unreg;
		}
		parent_names[i] = kstrdup_const(clks[i].name, GFP_KERNEL);
	}

	data->nparents = nparents;
	return parent_names;

err_unreg:
	plt_clk_unregister_fixed_rate_loop(data, i);
	plt_clk_free_parent_names_loop(parent_names, i);
	return ERR_PTR(err);
}

static void plt_clk_unregister_parents(struct clk_plt_data *data)
{
	plt_clk_unregister_fixed_rate_loop(data, data->nparents);
}

static int plt_clk_probe(struct platform_device *pdev)
{
	const struct pmc_clk_data *pmc_data;
	const char **parent_names;
	struct clk_plt_data *data;
	unsigned int i;
	int err;

	pmc_data = dev_get_platdata(&pdev->dev);
	if (!pmc_data || !pmc_data->clks)
		return -EINVAL;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	parent_names = plt_clk_register_parents(pdev, data, pmc_data->clks);
	if (IS_ERR(parent_names))
		return PTR_ERR(parent_names);

	for (i = 0; i < PMC_CLK_NUM; i++) {
		data->clks[i] = plt_clk_register(pdev, i, pmc_data->base,
						 parent_names, data->nparents);
		if (IS_ERR(data->clks[i])) {
			err = PTR_ERR(data->clks[i]);
			goto err_unreg_clk_plt;
		}
	}
	data->mclk_lookup = clkdev_hw_create(&data->clks[3]->hw, "mclk", NULL);
	if (!data->mclk_lookup) {
		err = -ENOMEM;
		goto err_unreg_clk_plt;
	}

	plt_clk_free_parent_names_loop(parent_names, data->nparents);

	platform_set_drvdata(pdev, data);
	return 0;

err_unreg_clk_plt:
	plt_clk_unregister_loop(data, i);
	plt_clk_unregister_parents(data);
	plt_clk_free_parent_names_loop(parent_names, data->nparents);
	return err;
}

static int plt_clk_remove(struct platform_device *pdev)
{
	struct clk_plt_data *data;

	data = platform_get_drvdata(pdev);

	clkdev_drop(data->mclk_lookup);
	plt_clk_unregister_loop(data, PMC_CLK_NUM);
	plt_clk_unregister_parents(data);
	return 0;
}

static struct platform_driver plt_clk_driver = {
	.driver = {
		.name = "clk-pmc-atom",
	},
	.probe = plt_clk_probe,
	.remove = plt_clk_remove,
};
builtin_platform_driver(plt_clk_driver);
