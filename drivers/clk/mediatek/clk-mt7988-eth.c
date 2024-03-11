// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Xiufeng Li <Xiufeng.Li@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include "reset.h"
#include <dt-bindings/clock/mediatek,mt7988-clk.h>
#include <dt-bindings/reset/mediatek,mt7988-resets.h>

static const struct mtk_gate_regs ethdma_cg_regs = {
	.set_ofs = 0x30,
	.clr_ofs = 0x30,
	.sta_ofs = 0x30,
};

#define GATE_ETHDMA(_id, _name, _parent, _shift)		\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &ethdma_cg_regs,			\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,		\
	}

static const struct mtk_gate ethdma_clks[] = {
	GATE_ETHDMA(CLK_ETHDMA_XGP1_EN, "ethdma_xgp1_en", "top_xtal", 0),
	GATE_ETHDMA(CLK_ETHDMA_XGP2_EN, "ethdma_xgp2_en", "top_xtal", 1),
	GATE_ETHDMA(CLK_ETHDMA_XGP3_EN, "ethdma_xgp3_en", "top_xtal", 2),
	GATE_ETHDMA(CLK_ETHDMA_FE_EN, "ethdma_fe_en", "netsys_2x_sel", 6),
	GATE_ETHDMA(CLK_ETHDMA_GP2_EN, "ethdma_gp2_en", "top_xtal", 7),
	GATE_ETHDMA(CLK_ETHDMA_GP1_EN, "ethdma_gp1_en", "top_xtal", 8),
	GATE_ETHDMA(CLK_ETHDMA_GP3_EN, "ethdma_gp3_en", "top_xtal", 10),
	GATE_ETHDMA(CLK_ETHDMA_ESW_EN, "ethdma_esw_en", "netsys_gsw_sel", 16),
	GATE_ETHDMA(CLK_ETHDMA_CRYPT0_EN, "ethdma_crypt0_en", "eip197_sel", 29),
};

static const struct mtk_clk_desc ethdma_desc = {
	.clks = ethdma_clks,
	.num_clks = ARRAY_SIZE(ethdma_clks),
};

static const struct mtk_gate_regs sgmii_cg_regs = {
	.set_ofs = 0xe4,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe4,
};

#define GATE_SGMII(_id, _name, _parent, _shift)			\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &sgmii_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,		\
	}

static const struct mtk_gate sgmii0_clks[] = {
	GATE_SGMII(CLK_SGM0_TX_EN, "sgm0_tx_en", "top_xtal", 2),
	GATE_SGMII(CLK_SGM0_RX_EN, "sgm0_rx_en", "top_xtal", 3),
};

static const struct mtk_clk_desc sgmii0_desc = {
	.clks = sgmii0_clks,
	.num_clks = ARRAY_SIZE(sgmii0_clks),
};

static const struct mtk_gate sgmii1_clks[] = {
	GATE_SGMII(CLK_SGM1_TX_EN, "sgm1_tx_en", "top_xtal", 2),
	GATE_SGMII(CLK_SGM1_RX_EN, "sgm1_rx_en", "top_xtal", 3),
};

static const struct mtk_clk_desc sgmii1_desc = {
	.clks = sgmii1_clks,
	.num_clks = ARRAY_SIZE(sgmii1_clks),
};

static const struct mtk_gate_regs ethwarp_cg_regs = {
	.set_ofs = 0x14,
	.clr_ofs = 0x14,
	.sta_ofs = 0x14,
};

#define GATE_ETHWARP(_id, _name, _parent, _shift)		\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &ethwarp_cg_regs,			\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,		\
	}

static const struct mtk_gate ethwarp_clks[] = {
	GATE_ETHWARP(CLK_ETHWARP_WOCPU2_EN, "ethwarp_wocpu2_en", "netsys_mcu_sel", 13),
	GATE_ETHWARP(CLK_ETHWARP_WOCPU1_EN, "ethwarp_wocpu1_en", "netsys_mcu_sel", 14),
	GATE_ETHWARP(CLK_ETHWARP_WOCPU0_EN, "ethwarp_wocpu0_en", "netsys_mcu_sel", 15),
};

static u16 ethwarp_rst_ofs[] = { 0x8 };

static u16 ethwarp_idx_map[] = {
	[MT7988_ETHWARP_RST_SWITCH] = 9,
};

static const struct mtk_clk_rst_desc ethwarp_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = ethwarp_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(ethwarp_rst_ofs),
	.rst_idx_map = ethwarp_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(ethwarp_idx_map),
};

static const struct mtk_clk_desc ethwarp_desc = {
	.clks = ethwarp_clks,
	.num_clks = ARRAY_SIZE(ethwarp_clks),
	.rst_desc = &ethwarp_rst_desc,
};

static const struct of_device_id of_match_clk_mt7988_eth[] = {
	{ .compatible = "mediatek,mt7988-ethsys", .data = &ethdma_desc },
	{ .compatible = "mediatek,mt7988-sgmiisys0", .data = &sgmii0_desc },
	{ .compatible = "mediatek,mt7988-sgmiisys1", .data = &sgmii1_desc },
	{ .compatible = "mediatek,mt7988-ethwarp", .data = &ethwarp_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7988_eth);

static struct platform_driver clk_mt7988_eth_drv = {
	.driver = {
		.name = "clk-mt7988-eth",
		.of_match_table = of_match_clk_mt7988_eth,
	},
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt7988_eth_drv);

MODULE_DESCRIPTION("MediaTek MT7988 Ethernet clocks driver");
MODULE_LICENSE("GPL");
