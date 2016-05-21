/*
 * Copyright (C) 2010 Broadcom
 * Copyright (C) 2012 Stephen Warren
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/stringify.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

/* Standard regmap gate clocks */
struct clk_oxnas {
	struct clk_hw hw;
	signed char bit;
	struct regmap *regmap;
};

/* Regmap offsets */
#define CLK_STAT_REGOFFSET	0x24
#define CLK_SET_REGOFFSET	0x2c
#define CLK_CLR_REGOFFSET	0x30

static inline struct clk_oxnas *to_clk_oxnas(struct clk_hw *hw)
{
	return container_of(hw, struct clk_oxnas, hw);
}

static int oxnas_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_oxnas *std = to_clk_oxnas(hw);
	int ret;
	unsigned int val;

	ret = regmap_read(std->regmap, CLK_STAT_REGOFFSET, &val);
	if (ret < 0)
		return ret;

	return val & BIT(std->bit);
}

static int oxnas_clk_enable(struct clk_hw *hw)
{
	struct clk_oxnas *std = to_clk_oxnas(hw);

	regmap_write(std->regmap, CLK_SET_REGOFFSET, BIT(std->bit));

	return 0;
}

static void oxnas_clk_disable(struct clk_hw *hw)
{
	struct clk_oxnas *std = to_clk_oxnas(hw);

	regmap_write(std->regmap, CLK_CLR_REGOFFSET, BIT(std->bit));
}

static const struct clk_ops oxnas_clk_ops = {
	.enable = oxnas_clk_enable,
	.disable = oxnas_clk_disable,
	.is_enabled = oxnas_clk_is_enabled,
};

static const char *const oxnas_clk_parents[] = {
	"oscillator",
};

static const char *const eth_parents[] = {
	"gmacclk",
};

#define DECLARE_STD_CLKP(__clk, __parent)			\
static const struct clk_init_data clk_##__clk##_init = {	\
	.name = __stringify(__clk),				\
	.ops = &oxnas_clk_ops,					\
	.parent_names = __parent,				\
	.num_parents = ARRAY_SIZE(__parent),			\
}

#define DECLARE_STD_CLK(__clk) DECLARE_STD_CLKP(__clk, oxnas_clk_parents)

/* Hardware Bit - Clock association */
struct clk_oxnas_init_data {
	unsigned long bit;
	const struct clk_init_data *clk_init;
};

/* Clk init data declaration */
DECLARE_STD_CLK(leon);
DECLARE_STD_CLK(dma_sgdma);
DECLARE_STD_CLK(cipher);
DECLARE_STD_CLK(sata);
DECLARE_STD_CLK(audio);
DECLARE_STD_CLK(usbmph);
DECLARE_STD_CLKP(etha, eth_parents);
DECLARE_STD_CLK(pciea);
DECLARE_STD_CLK(nand);

/* Table index is clock indice */
static const struct clk_oxnas_init_data clk_oxnas_init[] = {
	[0] = {0, &clk_leon_init},
	[1] = {1, &clk_dma_sgdma_init},
	[2] = {2, &clk_cipher_init},
	/* Skip & Do not touch to DDR clock */
	[3] = {4, &clk_sata_init},
	[4] = {5, &clk_audio_init},
	[5] = {6, &clk_usbmph_init},
	[6] = {7, &clk_etha_init},
	[7] = {8, &clk_pciea_init},
	[8] = {9, &clk_nand_init},
};

struct clk_oxnas_data {
	struct clk_oxnas clk_oxnas[ARRAY_SIZE(clk_oxnas_init)];
	struct clk_onecell_data onecell_data[ARRAY_SIZE(clk_oxnas_init)];
	struct clk *clks[ARRAY_SIZE(clk_oxnas_init)];
};

static int oxnas_stdclk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk_oxnas_data *clk_oxnas;
	struct regmap *regmap;
	int i;

	clk_oxnas = devm_kzalloc(&pdev->dev, sizeof(*clk_oxnas), GFP_KERNEL);
	if (!clk_oxnas)
		return -ENOMEM;

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (!regmap) {
		dev_err(&pdev->dev, "failed to have parent regmap\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(clk_oxnas_init); i++) {
		struct clk_oxnas *_clk;

		_clk = &clk_oxnas->clk_oxnas[i];
		_clk->bit = clk_oxnas_init[i].bit;
		_clk->hw.init = clk_oxnas_init[i].clk_init;
		_clk->regmap = regmap;

		clk_oxnas->clks[i] =
			devm_clk_register(&pdev->dev, &_clk->hw);
		if (WARN_ON(IS_ERR(clk_oxnas->clks[i])))
			return PTR_ERR(clk_oxnas->clks[i]);
	}

	clk_oxnas->onecell_data->clks = clk_oxnas->clks;
	clk_oxnas->onecell_data->clk_num = ARRAY_SIZE(clk_oxnas_init);

	return of_clk_add_provider(np, of_clk_src_onecell_get,
				   clk_oxnas->onecell_data);
}

static int oxnas_stdclk_remove(struct platform_device *pdev)
{
	of_clk_del_provider(pdev->dev.of_node);

	return 0;
}

static const struct of_device_id oxnas_stdclk_dt_ids[] = {
	{ .compatible = "oxsemi,ox810se-stdclk" },
	{ }
};
MODULE_DEVICE_TABLE(of, oxnas_stdclk_dt_ids);

static struct platform_driver oxnas_stdclk_driver = {
	.probe = oxnas_stdclk_probe,
	.remove = oxnas_stdclk_remove,
	.driver	= {
		.name = "oxnas-stdclk",
		.of_match_table = oxnas_stdclk_dt_ids,
	},
};

module_platform_driver(oxnas_stdclk_driver);
