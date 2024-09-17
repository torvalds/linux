// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Broadcom
 * Copyright (C) 2012 Stephen Warren
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/stringify.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <dt-bindings/clock/oxsemi,ox810se.h>
#include <dt-bindings/clock/oxsemi,ox820.h>

/* Standard regmap gate clocks */
struct clk_oxnas_gate {
	struct clk_hw hw;
	unsigned int bit;
	struct regmap *regmap;
};

struct oxnas_stdclk_data {
	struct clk_hw_onecell_data *onecell_data;
	struct clk_oxnas_gate **gates;
	unsigned int ngates;
	struct clk_oxnas_pll **plls;
	unsigned int nplls;
};

/* Regmap offsets */
#define CLK_STAT_REGOFFSET	0x24
#define CLK_SET_REGOFFSET	0x2c
#define CLK_CLR_REGOFFSET	0x30

static inline struct clk_oxnas_gate *to_clk_oxnas_gate(struct clk_hw *hw)
{
	return container_of(hw, struct clk_oxnas_gate, hw);
}

static int oxnas_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct clk_oxnas_gate *std = to_clk_oxnas_gate(hw);
	int ret;
	unsigned int val;

	ret = regmap_read(std->regmap, CLK_STAT_REGOFFSET, &val);
	if (ret < 0)
		return ret;

	return val & BIT(std->bit);
}

static int oxnas_clk_gate_enable(struct clk_hw *hw)
{
	struct clk_oxnas_gate *std = to_clk_oxnas_gate(hw);

	regmap_write(std->regmap, CLK_SET_REGOFFSET, BIT(std->bit));

	return 0;
}

static void oxnas_clk_gate_disable(struct clk_hw *hw)
{
	struct clk_oxnas_gate *std = to_clk_oxnas_gate(hw);

	regmap_write(std->regmap, CLK_CLR_REGOFFSET, BIT(std->bit));
}

static const struct clk_ops oxnas_clk_gate_ops = {
	.enable = oxnas_clk_gate_enable,
	.disable = oxnas_clk_gate_disable,
	.is_enabled = oxnas_clk_gate_is_enabled,
};

static const char *const osc_parents[] = {
	"oscillator",
};

static const char *const eth_parents[] = {
	"gmacclk",
};

#define OXNAS_GATE(_name, _bit, _parents)				\
struct clk_oxnas_gate _name = {						\
	.bit = (_bit),							\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = &oxnas_clk_gate_ops,				\
		.parent_names = _parents,				\
		.num_parents = ARRAY_SIZE(_parents),			\
		.flags = (CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),	\
	},								\
}

static OXNAS_GATE(ox810se_leon, 0, osc_parents);
static OXNAS_GATE(ox810se_dma_sgdma, 1, osc_parents);
static OXNAS_GATE(ox810se_cipher, 2, osc_parents);
static OXNAS_GATE(ox810se_sata, 4, osc_parents);
static OXNAS_GATE(ox810se_audio, 5, osc_parents);
static OXNAS_GATE(ox810se_usbmph, 6, osc_parents);
static OXNAS_GATE(ox810se_etha, 7, eth_parents);
static OXNAS_GATE(ox810se_pciea, 8, osc_parents);
static OXNAS_GATE(ox810se_nand, 9, osc_parents);

static struct clk_oxnas_gate *ox810se_gates[] = {
	&ox810se_leon,
	&ox810se_dma_sgdma,
	&ox810se_cipher,
	&ox810se_sata,
	&ox810se_audio,
	&ox810se_usbmph,
	&ox810se_etha,
	&ox810se_pciea,
	&ox810se_nand,
};

static OXNAS_GATE(ox820_leon, 0, osc_parents);
static OXNAS_GATE(ox820_dma_sgdma, 1, osc_parents);
static OXNAS_GATE(ox820_cipher, 2, osc_parents);
static OXNAS_GATE(ox820_sd, 3, osc_parents);
static OXNAS_GATE(ox820_sata, 4, osc_parents);
static OXNAS_GATE(ox820_audio, 5, osc_parents);
static OXNAS_GATE(ox820_usbmph, 6, osc_parents);
static OXNAS_GATE(ox820_etha, 7, eth_parents);
static OXNAS_GATE(ox820_pciea, 8, osc_parents);
static OXNAS_GATE(ox820_nand, 9, osc_parents);
static OXNAS_GATE(ox820_ethb, 10, eth_parents);
static OXNAS_GATE(ox820_pcieb, 11, osc_parents);
static OXNAS_GATE(ox820_ref600, 12, osc_parents);
static OXNAS_GATE(ox820_usbdev, 13, osc_parents);

static struct clk_oxnas_gate *ox820_gates[] = {
	&ox820_leon,
	&ox820_dma_sgdma,
	&ox820_cipher,
	&ox820_sd,
	&ox820_sata,
	&ox820_audio,
	&ox820_usbmph,
	&ox820_etha,
	&ox820_pciea,
	&ox820_nand,
	&ox820_etha,
	&ox820_pciea,
	&ox820_ref600,
	&ox820_usbdev,
};

static struct clk_hw_onecell_data ox810se_hw_onecell_data = {
	.hws = {
		[CLK_810_LEON]	= &ox810se_leon.hw,
		[CLK_810_DMA_SGDMA]	= &ox810se_dma_sgdma.hw,
		[CLK_810_CIPHER]	= &ox810se_cipher.hw,
		[CLK_810_SATA]	= &ox810se_sata.hw,
		[CLK_810_AUDIO]	= &ox810se_audio.hw,
		[CLK_810_USBMPH]	= &ox810se_usbmph.hw,
		[CLK_810_ETHA]	= &ox810se_etha.hw,
		[CLK_810_PCIEA]	= &ox810se_pciea.hw,
		[CLK_810_NAND]	= &ox810se_nand.hw,
	},
	.num = ARRAY_SIZE(ox810se_gates),
};

static struct clk_hw_onecell_data ox820_hw_onecell_data = {
	.hws = {
		[CLK_820_LEON]	= &ox820_leon.hw,
		[CLK_820_DMA_SGDMA]	= &ox820_dma_sgdma.hw,
		[CLK_820_CIPHER]	= &ox820_cipher.hw,
		[CLK_820_SD]	= &ox820_sd.hw,
		[CLK_820_SATA]	= &ox820_sata.hw,
		[CLK_820_AUDIO]	= &ox820_audio.hw,
		[CLK_820_USBMPH]	= &ox820_usbmph.hw,
		[CLK_820_ETHA]	= &ox820_etha.hw,
		[CLK_820_PCIEA]	= &ox820_pciea.hw,
		[CLK_820_NAND]	= &ox820_nand.hw,
		[CLK_820_ETHB]	= &ox820_ethb.hw,
		[CLK_820_PCIEB]	= &ox820_pcieb.hw,
		[CLK_820_REF600]	= &ox820_ref600.hw,
		[CLK_820_USBDEV]	= &ox820_usbdev.hw,
	},
	.num = ARRAY_SIZE(ox820_gates),
};

static struct oxnas_stdclk_data ox810se_stdclk_data = {
	.onecell_data = &ox810se_hw_onecell_data,
	.gates = ox810se_gates,
	.ngates = ARRAY_SIZE(ox810se_gates),
};

static struct oxnas_stdclk_data ox820_stdclk_data = {
	.onecell_data = &ox820_hw_onecell_data,
	.gates = ox820_gates,
	.ngates = ARRAY_SIZE(ox820_gates),
};

static const struct of_device_id oxnas_stdclk_dt_ids[] = {
	{ .compatible = "oxsemi,ox810se-stdclk", &ox810se_stdclk_data },
	{ .compatible = "oxsemi,ox820-stdclk", &ox820_stdclk_data },
	{ }
};

static int oxnas_stdclk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *parent_np;
	const struct oxnas_stdclk_data *data;
	struct regmap *regmap;
	int ret;
	int i;

	data = of_device_get_match_data(&pdev->dev);

	parent_np = of_get_parent(np);
	regmap = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "failed to have parent regmap\n");
		return PTR_ERR(regmap);
	}

	for (i = 0 ; i < data->ngates ; ++i)
		data->gates[i]->regmap = regmap;

	for (i = 0; i < data->onecell_data->num; i++) {
		if (!data->onecell_data->hws[i])
			continue;

		ret = devm_clk_hw_register(&pdev->dev,
					   data->onecell_data->hws[i]);
		if (ret)
			return ret;
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get,
				      data->onecell_data);
}

static struct platform_driver oxnas_stdclk_driver = {
	.probe = oxnas_stdclk_probe,
	.driver	= {
		.name = "oxnas-stdclk",
		.suppress_bind_attrs = true,
		.of_match_table = oxnas_stdclk_dt_ids,
	},
};
builtin_platform_driver(oxnas_stdclk_driver);
