// SPDX-License-Identifier: GPL-2.0
/*
 * R-Car X5H Clock Pulse Generator
 *
 * Copyright (C) 2026 Glider bv
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/dev_printk.h>
#include <linux/device-id/of.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/renesas,r8a78000-cpg.h>

struct clk_map {
	int dt_id;		/* DT binding clock ID or -1 sentinel */
	u32 fw_id;		/* FIXED_CLK() ID */
};

enum fixed_clk {
	FIXED_CLK_66M,
	FIXED_CLK_266M,
	NUM_FIXED_CLKS
};

static const unsigned long fixed_clk_rates[NUM_FIXED_CLKS] = {
	[FIXED_CLK_66M] = 66666000,
	[FIXED_CLK_266M] = 266660000,
};

#define FIXED_CLK(rate)		FIXED_CLK_ ## rate

/**
 * struct r8a78000_cpg_priv - Clock Pulse Generator Private Data
 *
 * @dev: CPG device
 * @map: Mapping from DT clock IDs to fixed-rate clocks
 * @fixed_hws: Fixed rate clocks
 */
struct r8a78000_cpg_priv {
	struct device *dev;
	const struct clk_map *map;
	struct clk_hw *fixed_hws[NUM_FIXED_CLKS];
};

static const struct clk_map *clk_map_find(const struct clk_map *map, u32 id)
{
	if (!map)
		return NULL;

	for (; map->dt_id >= 0; map++) {
		if (map->dt_id == id)
			return map;
	}

	return NULL;
}

static struct clk_hw *r8a78000_clk_get(struct of_phandle_args *spec,
				      void *data)
{
	struct r8a78000_cpg_priv *priv = data;
	struct device *dev = priv->dev;
	const struct clk_map *map;
	struct clk_hw *hw;
	u32 id;

	if (spec->args_count != 1)
		return ERR_PTR(-EINVAL);

	id = spec->args[0];

	map = clk_map_find(priv->map, id);
	if (!map) {
		dev_err(dev, "Unknown clock %u\n", id);
		return ERR_PTR(-ENOENT);
	}

	dev_dbg(dev, "Mapping DT clock %u to fixed clock %u\n", id, map->fw_id);

	hw = priv->fixed_hws[map->fw_id];

	dev_dbg(dev, "clock %u is %s at %lu Hz\n", id, clk_hw_get_name(hw),
		clk_hw_get_rate(hw));

	return hw;
}

static int register_fixed_clks(struct r8a78000_cpg_priv *priv)
{
	struct device *dev = priv->dev;
	unsigned long rate;
	struct clk_hw *hw;
	const char *name;

	for (unsigned int i = 0; i < ARRAY_SIZE(fixed_clk_rates); i++) {
		rate = fixed_clk_rates[i];
		name = devm_kasprintf(dev, GFP_KERNEL, "cpg-%lu", rate);
		if (!name)
			return -ENOMEM;

		hw = devm_clk_hw_register_fixed_rate(dev, name, NULL, 0, rate);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		priv->fixed_hws[i] = hw;
	}

	return 0;
}

static int r8a78000_cpg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct r8a78000_cpg_priv *priv;
	const struct clk_map *map;
	int ret;

	map = of_device_get_match_data(dev);
	if (!map)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->map = map;

	ret = register_fixed_clks(priv);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, r8a78000_clk_get, priv);
}

static const struct clk_map r8a78000_cpg_default[] = {
	{ R8A78000_CPG_SGASYNCD4_PERW_BUS,	FIXED_CLK(266M) },
	{ R8A78000_CPG_SGASYNCD16_PERW_BUS,	FIXED_CLK(66M) },
	{ -1 }
};

static const struct of_device_id r8a78000_cpg_match[] = {
	{
		.compatible = "renesas,r8a78000-cpg",
		.data = &r8a78000_cpg_default,
	},
	{ /* sentinel */ }
};

static struct platform_driver r8a78000_cpg_driver = {
	.probe = r8a78000_cpg_probe,
	.driver = {
		.name = "r8a78000-cpg",
		.of_match_table = r8a78000_cpg_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(r8a78000_cpg_driver)

MODULE_DESCRIPTION("R-Car X5H CPG Driver");
