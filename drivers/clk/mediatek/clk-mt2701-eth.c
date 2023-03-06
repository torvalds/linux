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

static const struct mtk_gate_regs eth_cg_regs = {
	.sta_ofs = 0x0030,
};

#define GATE_ETH(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &eth_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate eth_clks[] = {
	GATE_DUMMY(CLK_DUMMY, "eth_dummy"),
	GATE_ETH(CLK_ETHSYS_HSDMA, "hsdma_clk", "ethif_sel", 5),
	GATE_ETH(CLK_ETHSYS_ESW, "esw_clk", "ethpll_500m_ck", 6),
	GATE_ETH(CLK_ETHSYS_GP2, "gp2_clk", "trgpll", 7),
	GATE_ETH(CLK_ETHSYS_GP1, "gp1_clk", "ethpll_500m_ck", 8),
	GATE_ETH(CLK_ETHSYS_PCM, "pcm_clk", "ethif_sel", 11),
	GATE_ETH(CLK_ETHSYS_GDMA, "gdma_clk", "ethif_sel", 14),
	GATE_ETH(CLK_ETHSYS_I2S, "i2s_clk", "ethif_sel", 17),
	GATE_ETH(CLK_ETHSYS_CRYPTO, "crypto_clk", "ethif_sel", 29),
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

static const struct of_device_id of_match_clk_mt2701_eth[] = {
	{ .compatible = "mediatek,mt2701-ethsys", .data = &eth_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt2701_eth_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2701-eth",
		.of_match_table = of_match_clk_mt2701_eth,
	},
};

builtin_platform_driver(clk_mt2701_eth_drv);
