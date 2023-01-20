// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Shunli Wang <shunli.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2701-clk.h>

static const struct mtk_gate_regs hif_cg_regs = {
	.sta_ofs = 0x0030,
};

#define GATE_HIF(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &hif_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate hif_clks[] = {
	GATE_HIF(CLK_HIFSYS_USB0PHY, "usb0_phy_clk", "ethpll_500m_ck", 21),
	GATE_HIF(CLK_HIFSYS_USB1PHY, "usb1_phy_clk", "ethpll_500m_ck", 22),
	GATE_HIF(CLK_HIFSYS_PCIE0, "pcie0_clk", "ethpll_500m_ck", 24),
	GATE_HIF(CLK_HIFSYS_PCIE1, "pcie1_clk", "ethpll_500m_ck", 25),
	GATE_HIF(CLK_HIFSYS_PCIE2, "pcie2_clk", "ethpll_500m_ck", 26),
};

static u16 rst_ofs[] = { 0x34, };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(rst_ofs),
};

static const struct of_device_id of_match_clk_mt2701_hif[] = {
	{ .compatible = "mediatek,mt2701-hifsys", },
	{}
};

static int clk_mt2701_hif_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_HIFSYS_NR);

	mtk_clk_register_gates(&pdev->dev, node, hif_clks,
			       ARRAY_SIZE(hif_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r) {
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);
		return r;
	}

	mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc);

	return 0;
}

static struct platform_driver clk_mt2701_hif_drv = {
	.probe = clk_mt2701_hif_probe,
	.driver = {
		.name = "clk-mt2701-hif",
		.of_match_table = of_match_clk_mt2701_hif,
	},
};

builtin_platform_driver(clk_mt2701_hif_drv);
