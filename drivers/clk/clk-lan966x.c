// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Microchip LAN966x SoC Clock driver.
 *
 * Copyright (C) 2021 Microchip Technology, Inc. and its subsidiaries
 *
 * Author: Kavyasree Kotagiri <kavyasree.kotagiri@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/microchip,lan966x.h>

#define GCK_ENA         BIT(0)
#define GCK_SRC_SEL     GENMASK(9, 8)
#define GCK_PRESCALER   GENMASK(23, 16)

#define DIV_MAX		255

static const char *clk_names[N_CLOCKS] = {
	"qspi0", "qspi1", "qspi2", "sdmmc0",
	"pi", "mcan0", "mcan1", "flexcom0",
	"flexcom1", "flexcom2", "flexcom3",
	"flexcom4", "timer1", "usb_refclk",
};

struct lan966x_gck {
	struct clk_hw hw;
	void __iomem *reg;
};
#define to_lan966x_gck(hw) container_of(hw, struct lan966x_gck, hw)

static const struct clk_parent_data lan966x_gck_pdata[] = {
	{ .fw_name = "cpu", },
	{ .fw_name = "ddr", },
	{ .fw_name = "sys", },
};

static struct clk_init_data init = {
	.parent_data = lan966x_gck_pdata,
	.num_parents = ARRAY_SIZE(lan966x_gck_pdata),
};

struct clk_gate_soc_desc {
	const char *name;
	int bit_idx;
};

static const struct clk_gate_soc_desc clk_gate_desc[] = {
	{ "uhphs", 11 },
	{ "udphs", 10 },
	{ "mcramc", 9 },
	{ "hmatrix", 8 },
	{ }
};

static DEFINE_SPINLOCK(clk_gate_lock);
static void __iomem *base;

static int lan966x_gck_enable(struct clk_hw *hw)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 val = readl(gck->reg);

	val |= GCK_ENA;
	writel(val, gck->reg);

	return 0;
}

static void lan966x_gck_disable(struct clk_hw *hw)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 val = readl(gck->reg);

	val &= ~GCK_ENA;
	writel(val, gck->reg);
}

static int lan966x_gck_set_rate(struct clk_hw *hw,
				unsigned long rate,
				unsigned long parent_rate)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 div, val = readl(gck->reg);

	if (rate == 0 || parent_rate == 0)
		return -EINVAL;

	/* Set Prescalar */
	div = parent_rate / rate;
	val &= ~GCK_PRESCALER;
	val |= FIELD_PREP(GCK_PRESCALER, (div - 1));
	writel(val, gck->reg);

	return 0;
}

static long lan966x_gck_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	unsigned int div;

	if (rate == 0 || *parent_rate == 0)
		return -EINVAL;

	if (rate >= *parent_rate)
		return *parent_rate;

	div = DIV_ROUND_CLOSEST(*parent_rate, rate);

	return *parent_rate / div;
}

static unsigned long lan966x_gck_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 div, val = readl(gck->reg);

	div = FIELD_GET(GCK_PRESCALER, val);

	return parent_rate / (div + 1);
}

static int lan966x_gck_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req)
{
	struct clk_hw *parent;
	int i;

	for (i = 0; i < clk_hw_get_num_parents(hw); ++i) {
		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		/* Allowed prescaler divider range is 0-255 */
		if (clk_hw_get_rate(parent) / req->rate <= DIV_MAX) {
			req->best_parent_hw = parent;
			req->best_parent_rate = clk_hw_get_rate(parent);

			return 0;
		}
	}

	return -EINVAL;
}

static u8 lan966x_gck_get_parent(struct clk_hw *hw)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 val = readl(gck->reg);

	return FIELD_GET(GCK_SRC_SEL, val);
}

static int lan966x_gck_set_parent(struct clk_hw *hw, u8 index)
{
	struct lan966x_gck *gck = to_lan966x_gck(hw);
	u32 val = readl(gck->reg);

	val &= ~GCK_SRC_SEL;
	val |= FIELD_PREP(GCK_SRC_SEL, index);
	writel(val, gck->reg);

	return 0;
}

static const struct clk_ops lan966x_gck_ops = {
	.enable         = lan966x_gck_enable,
	.disable        = lan966x_gck_disable,
	.set_rate       = lan966x_gck_set_rate,
	.round_rate     = lan966x_gck_round_rate,
	.recalc_rate    = lan966x_gck_recalc_rate,
	.determine_rate = lan966x_gck_determine_rate,
	.set_parent     = lan966x_gck_set_parent,
	.get_parent     = lan966x_gck_get_parent,
};

static struct clk_hw *lan966x_gck_clk_register(struct device *dev, int i)
{
	struct lan966x_gck *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	priv->reg = base + (i * 4);
	priv->hw.init = &init;
	ret = devm_clk_hw_register(dev, &priv->hw);
	if (ret)
		return ERR_PTR(ret);

	return &priv->hw;
};

static int lan966x_gate_clk_register(struct device *dev,
				     struct clk_hw_onecell_data *hw_data,
				     void __iomem *gate_base)
{
	int i;

	for (i = GCK_GATE_UHPHS; i < N_CLOCKS; ++i) {
		int idx = i - GCK_GATE_UHPHS;

		hw_data->hws[i] =
			devm_clk_hw_register_gate(dev, clk_gate_desc[idx].name,
						  "lan966x", 0, gate_base,
						  clk_gate_desc[idx].bit_idx,
						  0, &clk_gate_lock);

		if (IS_ERR(hw_data->hws[i]))
			return dev_err_probe(dev, PTR_ERR(hw_data->hws[i]),
					     "failed to register %s clock\n",
					     clk_gate_desc[idx].name);
	}

	return 0;
}

static int lan966x_clk_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *hw_data;
	struct device *dev = &pdev->dev;
	void __iomem *gate_base;
	struct resource *res;
	int i, ret;

	hw_data = devm_kzalloc(dev, struct_size(hw_data, hws, N_CLOCKS),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	init.ops = &lan966x_gck_ops;

	hw_data->num = GCK_GATE_UHPHS;

	for (i = 0; i < GCK_GATE_UHPHS; i++) {
		init.name = clk_names[i];
		hw_data->hws[i] = lan966x_gck_clk_register(dev, i);
		if (IS_ERR(hw_data->hws[i])) {
			dev_err(dev, "failed to register %s clock\n",
				init.name);
			return PTR_ERR(hw_data->hws[i]);
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		gate_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(gate_base))
			return PTR_ERR(gate_base);

		hw_data->num = N_CLOCKS;

		ret = lan966x_gate_clk_register(dev, hw_data, gate_base);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, hw_data);
}

static const struct of_device_id lan966x_clk_dt_ids[] = {
	{ .compatible = "microchip,lan966x-gck", },
	{ }
};
MODULE_DEVICE_TABLE(of, lan966x_clk_dt_ids);

static struct platform_driver lan966x_clk_driver = {
	.probe  = lan966x_clk_probe,
	.driver = {
		.name = "lan966x-clk",
		.of_match_table = lan966x_clk_dt_ids,
	},
};
module_platform_driver(lan966x_clk_driver);

MODULE_AUTHOR("Kavyasree Kotagiri <kavyasree.kotagiri@microchip.com>");
MODULE_DESCRIPTION("LAN966X clock driver");
MODULE_LICENSE("GPL v2");
