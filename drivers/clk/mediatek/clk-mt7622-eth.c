// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt7622-clk.h>

#define GATE_ETH(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &eth_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate_regs eth_cg_regs = {
	.set_ofs = 0x30,
	.clr_ofs = 0x30,
	.sta_ofs = 0x30,
};

static const struct mtk_gate eth_clks[] = {
	GATE_ETH(CLK_ETH_HSDMA_EN, "eth_hsdma_en", "eth_sel", 5),
	GATE_ETH(CLK_ETH_ESW_EN, "eth_esw_en", "eth_500m", 6),
	GATE_ETH(CLK_ETH_GP2_EN, "eth_gp2_en", "txclk_src_pre", 7),
	GATE_ETH(CLK_ETH_GP1_EN, "eth_gp1_en", "txclk_src_pre", 8),
	GATE_ETH(CLK_ETH_GP0_EN, "eth_gp0_en", "txclk_src_pre", 9),
};

static const struct mtk_gate_regs sgmii_cg_regs = {
	.set_ofs = 0xE4,
	.clr_ofs = 0xE4,
	.sta_ofs = 0xE4,
};

#define GATE_SGMII(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &sgmii_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate sgmii_clks[] = {
	GATE_SGMII(CLK_SGMII_TX250M_EN, "sgmii_tx250m_en",
		   "ssusb_tx250m", 2),
	GATE_SGMII(CLK_SGMII_RX250M_EN, "sgmii_rx250m_en",
		   "ssusb_eq_rx250m", 3),
	GATE_SGMII(CLK_SGMII_CDR_REF, "sgmii_cdr_ref",
		   "ssusb_cdr_ref", 4),
	GATE_SGMII(CLK_SGMII_CDR_FB, "sgmii_cdr_fb",
		   "ssusb_cdr_fb", 5),
};

static u16 rst_ofs[] = { 0x34, };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(rst_ofs),
};

static const struct mtk_clk_desc eth_desc = {
	.clks = eth_clks,
	.num_clks = ARRAY_SIZE(eth_clks),
	.rst_desc = &clk_rst_desc,
};

static const struct mtk_clk_desc sgmii_desc = {
	.clks = sgmii_clks,
	.num_clks = ARRAY_SIZE(sgmii_clks),
};

static const struct of_device_id of_match_clk_mt7622_eth[] = {
	{ .compatible = "mediatek,mt7622-ethsys", .data = &eth_desc },
	{ .compatible = "mediatek,mt7622-sgmiisys", .data = &sgmii_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7622_eth);

static struct platform_driver clk_mt7622_eth_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt7622-eth",
		.of_match_table = of_match_clk_mt7622_eth,
	},
};
module_platform_driver(clk_mt7622_eth_drv);
MODULE_LICENSE("GPL");
