// SPDX-License-Identifier: GPL-2.0
/*
 * Clock driver for DA8xx/AM17xx/AM18xx/OMAP-L13x CFGCHIP
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/mfd/da8xx-cfgchip.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_data/clk-da8xx-cfgchip.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* --- Gate clocks --- */

#define DA8XX_GATE_CLOCK_IS_DIV4P5	BIT(1)

struct da8xx_cfgchip_gate_clk_info {
	const char *name;
	u32 cfgchip;
	u32 bit;
	u32 flags;
};

struct da8xx_cfgchip_gate_clk {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

#define to_da8xx_cfgchip_gate_clk(_hw) \
	container_of((_hw), struct da8xx_cfgchip_gate_clk, hw)

static int da8xx_cfgchip_gate_clk_enable(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);

	return regmap_write_bits(clk->regmap, clk->reg, clk->mask, clk->mask);
}

static void da8xx_cfgchip_gate_clk_disable(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);

	regmap_write_bits(clk->regmap, clk->reg, clk->mask, 0);
}

static int da8xx_cfgchip_gate_clk_is_enabled(struct clk_hw *hw)
{
	struct da8xx_cfgchip_gate_clk *clk = to_da8xx_cfgchip_gate_clk(hw);
	unsigned int val;

	regmap_read(clk->regmap, clk->reg, &val);

	return !!(val & clk->mask);
}

static unsigned long da8xx_cfgchip_div4p5_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)
{
	/* this clock divides by 4.5 */
	return parent_rate * 2 / 9;
}

static const struct clk_ops da8xx_cfgchip_gate_clk_ops = {
	.enable		= da8xx_cfgchip_gate_clk_enable,
	.disable	= da8xx_cfgchip_gate_clk_disable,
	.is_enabled	= da8xx_cfgchip_gate_clk_is_enabled,
};

static const struct clk_ops da8xx_cfgchip_div4p5_clk_ops = {
	.enable		= da8xx_cfgchip_gate_clk_enable,
	.disable	= da8xx_cfgchip_gate_clk_disable,
	.is_enabled	= da8xx_cfgchip_gate_clk_is_enabled,
	.recalc_rate	= da8xx_cfgchip_div4p5_recalc_rate,
};

static struct da8xx_cfgchip_gate_clk * __init
da8xx_cfgchip_gate_clk_register(struct device *dev,
				const struct da8xx_cfgchip_gate_clk_info *info,
				struct regmap *regmap)
{
	struct clk *parent;
	const char *parent_name;
	struct da8xx_cfgchip_gate_clk *gate;
	struct clk_init_data init;
	int ret;

	parent = devm_clk_get(dev, NULL);
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	gate = devm_kzalloc(dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = info->name;
	if (info->flags & DA8XX_GATE_CLOCK_IS_DIV4P5)
		init.ops = &da8xx_cfgchip_div4p5_clk_ops;
	else
		init.ops = &da8xx_cfgchip_gate_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = 0;

	gate->hw.init = &init;
	gate->regmap = regmap;
	gate->reg = info->cfgchip;
	gate->mask = info->bit;

	ret = devm_clk_hw_register(dev, &gate->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return gate;
}

static const struct da8xx_cfgchip_gate_clk_info da8xx_tbclksync_info __initconst = {
	.name = "ehrpwm_tbclk",
	.cfgchip = CFGCHIP(1),
	.bit = CFGCHIP1_TBCLKSYNC,
};

static int __init da8xx_cfgchip_register_tbclk(struct device *dev,
					       struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, &da8xx_tbclksync_info,
					       regmap);
	if (IS_ERR(gate))
		return PTR_ERR(gate);

	clk_hw_register_clkdev(&gate->hw, "tbclk", "ehrpwm.0");
	clk_hw_register_clkdev(&gate->hw, "tbclk", "ehrpwm.1");

	return 0;
}

static const struct da8xx_cfgchip_gate_clk_info da8xx_div4p5ena_info __initconst = {
	.name = "div4.5",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_DIV45PENA,
	.flags = DA8XX_GATE_CLOCK_IS_DIV4P5,
};

static int __init da8xx_cfgchip_register_div4p5(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, &da8xx_div4p5ena_info, regmap);
	if (IS_ERR(gate))
		return PTR_ERR(gate);

	return 0;
}

static int __init
of_da8xx_cfgchip_gate_clk_init(struct device *dev,
			       const struct da8xx_cfgchip_gate_clk_info *info,
			       struct regmap *regmap)
{
	struct da8xx_cfgchip_gate_clk *gate;

	gate = da8xx_cfgchip_gate_clk_register(dev, info, regmap);
	if (IS_ERR(gate))
		return PTR_ERR(gate);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, gate);
}

static int __init of_da8xx_tbclksync_init(struct device *dev,
					  struct regmap *regmap)
{
	return of_da8xx_cfgchip_gate_clk_init(dev, &da8xx_tbclksync_info, regmap);
}

static int __init of_da8xx_div4p5ena_init(struct device *dev,
					  struct regmap *regmap)
{
	return of_da8xx_cfgchip_gate_clk_init(dev, &da8xx_div4p5ena_info, regmap);
}

/* --- MUX clocks --- */

struct da8xx_cfgchip_mux_clk_info {
	const char *name;
	const char *parent0;
	const char *parent1;
	u32 cfgchip;
	u32 bit;
};

struct da8xx_cfgchip_mux_clk {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

#define to_da8xx_cfgchip_mux_clk(_hw) \
	container_of((_hw), struct da8xx_cfgchip_mux_clk, hw)

static int da8xx_cfgchip_mux_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct da8xx_cfgchip_mux_clk *clk = to_da8xx_cfgchip_mux_clk(hw);
	unsigned int val = index ? clk->mask : 0;

	return regmap_write_bits(clk->regmap, clk->reg, clk->mask, val);
}

static u8 da8xx_cfgchip_mux_clk_get_parent(struct clk_hw *hw)
{
	struct da8xx_cfgchip_mux_clk *clk = to_da8xx_cfgchip_mux_clk(hw);
	unsigned int val;

	regmap_read(clk->regmap, clk->reg, &val);

	return (val & clk->mask) ? 1 : 0;
}

static const struct clk_ops da8xx_cfgchip_mux_clk_ops = {
	.set_parent	= da8xx_cfgchip_mux_clk_set_parent,
	.get_parent	= da8xx_cfgchip_mux_clk_get_parent,
};

static struct da8xx_cfgchip_mux_clk * __init
da8xx_cfgchip_mux_clk_register(struct device *dev,
			       const struct da8xx_cfgchip_mux_clk_info *info,
			       struct regmap *regmap)
{
	const char * const parent_names[] = { info->parent0, info->parent1 };
	struct da8xx_cfgchip_mux_clk *mux;
	struct clk_init_data init;
	int ret;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return ERR_PTR(-ENOMEM);

	init.name = info->name;
	init.ops = &da8xx_cfgchip_mux_clk_ops;
	init.parent_names = parent_names;
	init.num_parents = 2;
	init.flags = 0;

	mux->hw.init = &init;
	mux->regmap = regmap;
	mux->reg = info->cfgchip;
	mux->mask = info->bit;

	ret = devm_clk_hw_register(dev, &mux->hw);
	if (ret < 0)
		return ERR_PTR(ret);

	return mux;
}

static const struct da8xx_cfgchip_mux_clk_info da850_async1_info __initconst = {
	.name = "async1",
	.parent0 = "pll0_sysclk3",
	.parent1 = "div4.5",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_EMA_CLKSRC,
};

static int __init da8xx_cfgchip_register_async1(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;

	mux = da8xx_cfgchip_mux_clk_register(dev, &da850_async1_info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	clk_hw_register_clkdev(&mux->hw, "async1", "da850-psc0");

	return 0;
}

static const struct da8xx_cfgchip_mux_clk_info da850_async3_info __initconst = {
	.name = "async3",
	.parent0 = "pll0_sysclk2",
	.parent1 = "pll1_sysclk2",
	.cfgchip = CFGCHIP(3),
	.bit = CFGCHIP3_ASYNC3_CLKSRC,
};

static int __init da850_cfgchip_register_async3(struct device *dev,
						struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;
	struct clk_hw *parent;

	mux = da8xx_cfgchip_mux_clk_register(dev, &da850_async3_info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	clk_hw_register_clkdev(&mux->hw, "async3", "da850-psc1");

	/* pll1_sysclk2 is not affected by CPU scaling, so use it for async3 */
	parent = clk_hw_get_parent_by_index(&mux->hw, 1);
	if (parent)
		clk_set_parent(mux->hw.clk, parent->clk);
	else
		dev_warn(dev, "Failed to find async3 parent clock\n");

	return 0;
}

static int __init
of_da8xx_cfgchip_init_mux_clock(struct device *dev,
				const struct da8xx_cfgchip_mux_clk_info *info,
				struct regmap *regmap)
{
	struct da8xx_cfgchip_mux_clk *mux;

	mux = da8xx_cfgchip_mux_clk_register(dev, info, regmap);
	if (IS_ERR(mux))
		return PTR_ERR(mux);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &mux->hw);
}

static int __init of_da850_async1_init(struct device *dev, struct regmap *regmap)
{
	return of_da8xx_cfgchip_init_mux_clock(dev, &da850_async1_info, regmap);
}

static int __init of_da850_async3_init(struct device *dev, struct regmap *regmap)
{
	return of_da8xx_cfgchip_init_mux_clock(dev, &da850_async3_info, regmap);
}

/* --- platform device --- */

static const struct of_device_id da8xx_cfgchip_of_match[] = {
	{
		.compatible = "ti,da830-tbclksync",
		.data = of_da8xx_tbclksync_init,
	},
	{
		.compatible = "ti,da830-div4p5ena",
		.data = of_da8xx_div4p5ena_init,
	},
	{
		.compatible = "ti,da850-async1-clksrc",
		.data = of_da850_async1_init,
	},
	{
		.compatible = "ti,da850-async3-clksrc",
		.data = of_da850_async3_init,
	},
	{ }
};

static const struct platform_device_id da8xx_cfgchip_id_table[] = {
	{
		.name = "da830-tbclksync",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_tbclk,
	},
	{
		.name = "da830-div4p5ena",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_div4p5,
	},
	{
		.name = "da850-async1-clksrc",
		.driver_data = (kernel_ulong_t)da8xx_cfgchip_register_async1,
	},
	{
		.name = "da850-async3-clksrc",
		.driver_data = (kernel_ulong_t)da850_cfgchip_register_async3,
	},
	{ }
};

typedef int (*da8xx_cfgchip_init)(struct device *dev, struct regmap *regmap);

static int da8xx_cfgchip_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct da8xx_cfgchip_clk_platform_data *pdata = dev->platform_data;
	const struct of_device_id *of_id;
	da8xx_cfgchip_init clk_init = NULL;
	struct regmap *regmap = NULL;

	of_id = of_match_device(da8xx_cfgchip_of_match, dev);
	if (of_id) {
		struct device_node *parent;

		clk_init = of_id->data;
		parent = of_get_parent(dev->of_node);
		regmap = syscon_node_to_regmap(parent);
		of_node_put(parent);
	} else if (pdev->id_entry && pdata) {
		clk_init = (void *)pdev->id_entry->driver_data;
		regmap = pdata->cfgchip;
	}

	if (!clk_init) {
		dev_err(dev, "unable to find driver data\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(regmap)) {
		dev_err(dev, "no regmap for CFGCHIP syscon\n");
		return regmap ? PTR_ERR(regmap) : -ENOENT;
	}

	return clk_init(dev, regmap);
}

static struct platform_driver da8xx_cfgchip_driver = {
	.probe		= da8xx_cfgchip_probe,
	.driver		= {
		.name		= "da8xx-cfgchip-clk",
		.of_match_table	= da8xx_cfgchip_of_match,
	},
	.id_table	= da8xx_cfgchip_id_table,
};

static int __init da8xx_cfgchip_driver_init(void)
{
	return platform_driver_register(&da8xx_cfgchip_driver);
}

/* has to be postcore_initcall because PSC devices depend on the async3 clock */
postcore_initcall(da8xx_cfgchip_driver_init);
