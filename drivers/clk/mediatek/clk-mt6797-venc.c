// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6797-clk.h>

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_VENC(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &venc_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr_inv,	\
	}

static const struct mtk_gate venc_clks[] = {
	GATE_VENC(CLK_VENC_0, "venc_0", "mm_sel", 0),
	GATE_VENC(CLK_VENC_1, "venc_1", "venc_sel", 4),
	GATE_VENC(CLK_VENC_2, "venc_2", "venc_sel", 8),
	GATE_VENC(CLK_VENC_3, "venc_3", "venc_sel", 12),
};

static const struct of_device_id of_match_clk_mt6797_venc[] = {
	{ .compatible = "mediatek,mt6797-vencsys", },
	{}
};

static int clk_mt6797_venc_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_VENC_NR);

	mtk_clk_register_gates(node, venc_clks, ARRAY_SIZE(venc_clks),
			       clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt6797_venc_drv = {
	.probe = clk_mt6797_venc_probe,
	.driver = {
		.name = "clk-mt6797-venc",
		.of_match_table = of_match_clk_mt6797_venc,
	},
};

builtin_platform_driver(clk_mt6797_venc_drv);
