// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com/
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

struct ti_syscon_gate_clk_priv {
	struct clk_hw hw;
	struct regmap *regmap;
	u32 reg;
	u32 idx;
};

struct ti_syscon_gate_clk_data {
	char *name;
	u32 offset;
	u32 bit_idx;
};

static struct
ti_syscon_gate_clk_priv *to_ti_syscon_gate_clk_priv(struct clk_hw *hw)
{
	return container_of(hw, struct ti_syscon_gate_clk_priv, hw);
}

static int ti_syscon_gate_clk_enable(struct clk_hw *hw)
{
	struct ti_syscon_gate_clk_priv *priv = to_ti_syscon_gate_clk_priv(hw);

	return regmap_write_bits(priv->regmap, priv->reg, priv->idx,
				 priv->idx);
}

static void ti_syscon_gate_clk_disable(struct clk_hw *hw)
{
	struct ti_syscon_gate_clk_priv *priv = to_ti_syscon_gate_clk_priv(hw);

	regmap_write_bits(priv->regmap, priv->reg, priv->idx, 0);
}

static int ti_syscon_gate_clk_is_enabled(struct clk_hw *hw)
{
	unsigned int val;
	struct ti_syscon_gate_clk_priv *priv = to_ti_syscon_gate_clk_priv(hw);

	regmap_read(priv->regmap, priv->reg, &val);

	return !!(val & priv->idx);
}

static const struct clk_ops ti_syscon_gate_clk_ops = {
	.enable		= ti_syscon_gate_clk_enable,
	.disable	= ti_syscon_gate_clk_disable,
	.is_enabled	= ti_syscon_gate_clk_is_enabled,
};

static struct clk_hw
*ti_syscon_gate_clk_register(struct device *dev, struct regmap *regmap,
			     const struct ti_syscon_gate_clk_data *data)
{
	struct ti_syscon_gate_clk_priv *priv;
	struct clk_init_data init;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	init.name = data->name;
	init.ops = &ti_syscon_gate_clk_ops;
	init.parent_names = NULL;
	init.num_parents = 0;
	init.flags = 0;

	priv->regmap = regmap;
	priv->reg = data->offset;
	priv->idx = BIT(data->bit_idx);
	priv->hw.init = &init;

	ret = devm_clk_hw_register(dev, &priv->hw);
	if (ret)
		return ERR_PTR(ret);

	return &priv->hw;
}

static int ti_syscon_gate_clk_probe(struct platform_device *pdev)
{
	const struct ti_syscon_gate_clk_data *data, *p;
	struct clk_hw_onecell_data *hw_data;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	int num_clks, i;

	data = device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(regmap)) {
		if (PTR_ERR(regmap) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(dev, "failed to find parent regmap\n");
		return PTR_ERR(regmap);
	}

	num_clks = 0;
	for (p = data; p->name; p++)
		num_clks++;

	hw_data = devm_kzalloc(dev, struct_size(hw_data, hws, num_clks),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	hw_data->num = num_clks;

	for (i = 0; i < num_clks; i++) {
		hw_data->hws[i] = ti_syscon_gate_clk_register(dev, regmap,
							      &data[i]);
		if (IS_ERR(hw_data->hws[i]))
			dev_warn(dev, "failed to register %s\n",
				 data[i].name);
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   hw_data);
}

#define TI_SYSCON_CLK_GATE(_name, _offset, _bit_idx)	\
	{						\
		.name = _name,				\
		.offset = (_offset),			\
		.bit_idx = (_bit_idx),			\
	}

static const struct ti_syscon_gate_clk_data am654_clk_data[] = {
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk0", 0x0, 0),
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk1", 0x4, 0),
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk2", 0x8, 0),
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk3", 0xc, 0),
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk4", 0x10, 0),
	TI_SYSCON_CLK_GATE("ehrpwm_tbclk5", 0x14, 0),
	{ /* Sentinel */ },
};

static const struct ti_syscon_gate_clk_data am64_clk_data[] = {
	TI_SYSCON_CLK_GATE("epwm_tbclk0", 0x0, 0),
	TI_SYSCON_CLK_GATE("epwm_tbclk1", 0x0, 1),
	TI_SYSCON_CLK_GATE("epwm_tbclk2", 0x0, 2),
	TI_SYSCON_CLK_GATE("epwm_tbclk3", 0x0, 3),
	TI_SYSCON_CLK_GATE("epwm_tbclk4", 0x0, 4),
	TI_SYSCON_CLK_GATE("epwm_tbclk5", 0x0, 5),
	TI_SYSCON_CLK_GATE("epwm_tbclk6", 0x0, 6),
	TI_SYSCON_CLK_GATE("epwm_tbclk7", 0x0, 7),
	TI_SYSCON_CLK_GATE("epwm_tbclk8", 0x0, 8),
	{ /* Sentinel */ },
};

static const struct ti_syscon_gate_clk_data am62_clk_data[] = {
	TI_SYSCON_CLK_GATE("epwm_tbclk0", 0x0, 0),
	TI_SYSCON_CLK_GATE("epwm_tbclk1", 0x0, 1),
	TI_SYSCON_CLK_GATE("epwm_tbclk2", 0x0, 2),
	{ /* Sentinel */ },
};

static const struct of_device_id ti_syscon_gate_clk_ids[] = {
	{
		.compatible = "ti,am654-ehrpwm-tbclk",
		.data = &am654_clk_data,
	},
	{
		.compatible = "ti,am64-epwm-tbclk",
		.data = &am64_clk_data,
	},
	{
		.compatible = "ti,am62-epwm-tbclk",
		.data = &am62_clk_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ti_syscon_gate_clk_ids);

static struct platform_driver ti_syscon_gate_clk_driver = {
	.probe = ti_syscon_gate_clk_probe,
	.driver = {
		.name = "ti-syscon-gate-clk",
		.of_match_table = ti_syscon_gate_clk_ids,
	},
};
module_platform_driver(ti_syscon_gate_clk_driver);

MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
MODULE_DESCRIPTION("Syscon backed gate-clock driver");
MODULE_LICENSE("GPL");
