/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt7622-clk.h>

#define GATE_ETH(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &eth_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,	\
	}

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

#define GATE_SGMII(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &sgmii_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,	\
	}

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

static int clk_mt7622_ethsys_init(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_ETH_NR_CLK);

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

static int clk_mt7622_sgmiisys_init(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_SGMII_NR_CLK);

	mtk_clk_register_gates(node, sgmii_clks, ARRAY_SIZE(sgmii_clks),
			       clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static const struct of_device_id of_match_clk_mt7622_eth[] = {
	{
		.compatible = "mediatek,mt7622-ethsys",
		.data = clk_mt7622_ethsys_init,
	}, {
		.compatible = "mediatek,mt7622-sgmiisys",
		.data = clk_mt7622_sgmiisys_init,
	}, {
		/* sentinel */
	}
};

static int clk_mt7622_eth_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *);
	int r;

	clk_init = of_device_get_match_data(&pdev->dev);
	if (!clk_init)
		return -EINVAL;

	r = clk_init(pdev);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt7622_eth_drv = {
	.probe = clk_mt7622_eth_probe,
	.driver = {
		.name = "clk-mt7622-eth",
		.of_match_table = of_match_clk_mt7622_eth,
	},
};

builtin_platform_driver(clk_mt7622_eth_drv);
