// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Wenzhen Yu <wenzhen.yu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt7986-clk.h>

static const struct mtk_gate_regs sgmii0_cg_regs = {
	.set_ofs = 0xe4,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe4,
};

#define GATE_SGMII0(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &sgmii0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate sgmii0_clks[] = {
	GATE_SGMII0(CLK_SGMII0_TX250M_EN, "sgmii0_tx250m_en", "top_xtal", 2),
	GATE_SGMII0(CLK_SGMII0_RX250M_EN, "sgmii0_rx250m_en", "top_xtal", 3),
	GATE_SGMII0(CLK_SGMII0_CDR_REF, "sgmii0_cdr_ref", "top_xtal", 4),
	GATE_SGMII0(CLK_SGMII0_CDR_FB, "sgmii0_cdr_fb", "top_xtal", 5),
};

static const struct mtk_gate_regs sgmii1_cg_regs = {
	.set_ofs = 0xe4,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe4,
};

#define GATE_SGMII1(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &sgmii1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate sgmii1_clks[] = {
	GATE_SGMII1(CLK_SGMII1_TX250M_EN, "sgmii1_tx250m_en", "top_xtal", 2),
	GATE_SGMII1(CLK_SGMII1_RX250M_EN, "sgmii1_rx250m_en", "top_xtal", 3),
	GATE_SGMII1(CLK_SGMII1_CDR_REF, "sgmii1_cdr_ref", "top_xtal", 4),
	GATE_SGMII1(CLK_SGMII1_CDR_FB, "sgmii1_cdr_fb", "top_xtal", 5),
};

static const struct mtk_gate_regs eth_cg_regs = {
	.set_ofs = 0x30,
	.clr_ofs = 0x30,
	.sta_ofs = 0x30,
};

#define GATE_ETH(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &eth_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate eth_clks[] = {
	GATE_ETH(CLK_ETH_FE_EN, "eth_fe_en", "netsys_2x_sel", 6),
	GATE_ETH(CLK_ETH_GP2_EN, "eth_gp2_en", "sgm_325m_sel", 7),
	GATE_ETH(CLK_ETH_GP1_EN, "eth_gp1_en", "sgm_325m_sel", 8),
	GATE_ETH(CLK_ETH_WOCPU1_EN, "eth_wocpu1_en", "netsys_mcu_sel", 14),
	GATE_ETH(CLK_ETH_WOCPU0_EN, "eth_wocpu0_en", "netsys_mcu_sel", 15),
};

static const struct mtk_clk_desc eth_desc = {
	.clks = eth_clks,
	.num_clks = ARRAY_SIZE(eth_clks),
};

static const struct mtk_clk_desc sgmii0_desc = {
	.clks = sgmii0_clks,
	.num_clks = ARRAY_SIZE(sgmii0_clks),
};

static const struct mtk_clk_desc sgmii1_desc = {
	.clks = sgmii1_clks,
	.num_clks = ARRAY_SIZE(sgmii1_clks),
};

static const struct of_device_id of_match_clk_mt7986_eth[] = {
	{ .compatible = "mediatek,mt7986-ethsys", .data = &eth_desc },
	{ .compatible = "mediatek,mt7986-sgmiisys_0", .data = &sgmii0_desc },
	{ .compatible = "mediatek,mt7986-sgmiisys_1", .data = &sgmii1_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7986_eth);

static struct platform_driver clk_mt7986_eth_drv = {
	.driver = {
		.name = "clk-mt7986-eth",
		.of_match_table = of_match_clk_mt7986_eth,
	},
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt7986_eth_drv);

MODULE_DESCRIPTION("MediaTek MT7986 Ethernet clocks driver");
MODULE_LICENSE("GPL");
