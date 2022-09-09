// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8183-clk.h>

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC_I(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &venc_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc_clks[] = {
	GATE_VENC_I(CLK_VENC_LARB, "venc_larb",
		"mm_sel", 0),
	GATE_VENC_I(CLK_VENC_VENC, "venc_venc",
		"mm_sel", 4),
	GATE_VENC_I(CLK_VENC_JPGENC, "venc_jpgenc",
		"mm_sel", 8),
};

static int clk_mt8183_venc_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_VENC_NR_CLK);

	mtk_clk_register_gates(node, venc_clks, ARRAY_SIZE(venc_clks),
			clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static const struct of_device_id of_match_clk_mt8183_venc[] = {
	{ .compatible = "mediatek,mt8183-vencsys", },
	{}
};

static struct platform_driver clk_mt8183_venc_drv = {
	.probe = clk_mt8183_venc_probe,
	.driver = {
		.name = "clk-mt8183-venc",
		.of_match_table = of_match_clk_mt8183_venc,
	},
};

builtin_platform_driver(clk_mt8183_venc_drv);
