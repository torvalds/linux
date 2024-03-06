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
	GATE_DUMMY(CLK_DUMMY, "hif_dummy"),
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

static const struct mtk_clk_desc hif_desc = {
	.clks = hif_clks,
	.num_clks = ARRAY_SIZE(hif_clks),
	.rst_desc = &clk_rst_desc,
};

static const struct of_device_id of_match_clk_mt2701_hif[] = {
	{ .compatible = "mediatek,mt2701-hifsys", .data = &hif_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt2701_hif);

static struct platform_driver clk_mt2701_hif_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2701-hif",
		.of_match_table = of_match_clk_mt2701_hif,
	},
};
module_platform_driver(clk_mt2701_hif_drv);
MODULE_LICENSE("GPL");
