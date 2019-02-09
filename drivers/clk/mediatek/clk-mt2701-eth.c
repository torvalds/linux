/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Shunli Wang <shunli.wang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2701-clk.h>

static const struct mtk_gate_regs eth_cg_regs = {
	.sta_ofs = 0x0030,
};

#define GATE_ETH(_id, _name, _parent, _shift) {		\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &eth_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,	\
	}

static const struct mtk_gate eth_clks[] = {
	GATE_ETH(CLK_ETHSYS_HSDMA, "hsdma_clk", "ethif_sel", 5),
	GATE_ETH(CLK_ETHSYS_ESW, "esw_clk", "ethpll_500m_ck", 6),
	GATE_ETH(CLK_ETHSYS_GP2, "gp2_clk", "trgpll", 7),
	GATE_ETH(CLK_ETHSYS_GP1, "gp1_clk", "ethpll_500m_ck", 8),
	GATE_ETH(CLK_ETHSYS_PCM, "pcm_clk", "ethif_sel", 11),
	GATE_ETH(CLK_ETHSYS_GDMA, "gdma_clk", "ethif_sel", 14),
	GATE_ETH(CLK_ETHSYS_I2S, "i2s_clk", "ethif_sel", 17),
	GATE_ETH(CLK_ETHSYS_CRYPTO, "crypto_clk", "ethif_sel", 29),
};

static const struct of_device_id of_match_clk_mt2701_eth[] = {
	{ .compatible = "mediatek,mt2701-ethsys", },
	{}
};

static int clk_mt2701_eth_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_ETHSYS_NR);

	mtk_clk_register_gates(node, eth_clks, ARRAY_SIZE(eth_clks),
						clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	mtk_register_reset_controller(node, 1, 0x34);

	return r;
}

static struct platform_driver clk_mt2701_eth_drv = {
	.probe = clk_mt2701_eth_probe,
	.driver = {
		.name = "clk-mt2701-eth",
		.of_match_table = of_match_clk_mt2701_eth,
	},
};

builtin_platform_driver(clk_mt2701_eth_drv);
