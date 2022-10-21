// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-mtk.h"

static const char * const mcu_armpll_ll_parents[] = {
	"clk26m",
	"armpll_ll",
	"mainpll",
	"univpll_d2"
};

static const char * const mcu_armpll_bl_parents[] = {
	"clk26m",
	"armpll_bl",
	"mainpll",
	"univpll_d2"
};

static const char * const mcu_armpll_bus_parents[] = {
	"clk26m",
	"ccipll",
	"mainpll",
	"univpll_d2"
};

/*
 * We only configure the CPU muxes when adjust CPU frequency in MediaTek CPUFreq Driver.
 * Other fields like divider always keep the same value. (set once in bootloader)
 */
static struct mtk_composite mcu_muxes[] = {
	/* CPU_PLLDIV_CFG0 */
	MUX(CLK_MCU_ARMPLL_LL_SEL, "mcu_armpll_ll_sel", mcu_armpll_ll_parents, 0x2A0, 9, 2),
	/* CPU_PLLDIV_CFG1 */
	MUX(CLK_MCU_ARMPLL_BL_SEL, "mcu_armpll_bl_sel", mcu_armpll_bl_parents, 0x2A4, 9, 2),
	/* BUS_PLLDIV_CFG */
	MUX(CLK_MCU_ARMPLL_BUS_SEL, "mcu_armpll_bus_sel", mcu_armpll_bus_parents, 0x2E0, 9, 2),
};

static const struct of_device_id of_match_clk_mt8186_mcu[] = {
	{ .compatible = "mediatek,mt8186-mcusys", },
	{}
};

static int clk_mt8186_mcu_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;
	void __iomem *base;

	clk_data = mtk_alloc_clk_data(CLK_MCU_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		r = PTR_ERR(base);
		goto free_mcu_data;
	}

	r = mtk_clk_register_composites(mcu_muxes, ARRAY_SIZE(mcu_muxes), base,
					NULL, clk_data);
	if (r)
		goto free_mcu_data;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_composite_muxes;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_composite_muxes:
	mtk_clk_unregister_composites(mcu_muxes, ARRAY_SIZE(mcu_muxes), clk_data);
free_mcu_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static int clk_mt8186_mcu_remove(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_composites(mcu_muxes, ARRAY_SIZE(mcu_muxes), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static struct platform_driver clk_mt8186_mcu_drv = {
	.probe = clk_mt8186_mcu_probe,
	.remove = clk_mt8186_mcu_remove,
	.driver = {
		.name = "clk-mt8186-mcu",
		.of_match_table = of_match_clk_mt8186_mcu,
	},
};
builtin_platform_driver(clk_mt8186_mcu_drv);
