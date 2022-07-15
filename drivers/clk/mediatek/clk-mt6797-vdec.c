// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin-CW Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6797-clk.h>

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0000,
	.clr_ofs = 0x0004,
	.sta_ofs = 0x0000,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x000c,
	.sta_ofs = 0x0008,
};

#define GATE_VDEC0(_id, _name, _parent, _shift) {		\
	.id = _id,					\
	.name = _name,					\
	.parent_name = _parent,				\
	.regs = &vdec0_cg_regs,				\
	.shift = _shift,				\
	.ops = &mtk_clk_gate_ops_setclr_inv,		\
}

#define GATE_VDEC1(_id, _name, _parent, _shift) {		\
	.id = _id,					\
	.name = _name,					\
	.parent_name = _parent,				\
	.regs = &vdec1_cg_regs,				\
	.shift = _shift,				\
	.ops = &mtk_clk_gate_ops_setclr_inv,		\
}

static const struct mtk_gate vdec_clks[] = {
	GATE_VDEC0(CLK_VDEC_CKEN_ENG, "vdec_cken_eng", "vdec_sel", 8),
	GATE_VDEC0(CLK_VDEC_ACTIVE, "vdec_active", "vdec_sel", 4),
	GATE_VDEC0(CLK_VDEC_CKEN, "vdec_cken", "vdec_sel", 0),
	GATE_VDEC1(CLK_VDEC_LARB1_CKEN, "vdec_larb1_cken", "mm_sel", 0),
};

static const struct of_device_id of_match_clk_mt6797_vdec[] = {
	{ .compatible = "mediatek,mt6797-vdecsys", },
	{}
};

static int clk_mt6797_vdec_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_VDEC_NR);

	mtk_clk_register_gates(node, vdec_clks, ARRAY_SIZE(vdec_clks),
			       clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt6797_vdec_drv = {
	.probe = clk_mt6797_vdec_probe,
	.driver = {
		.name = "clk-mt6797-vdec",
		.of_match_table = of_match_clk_mt6797_vdec,
	},
};

builtin_platform_driver(clk_mt6797_vdec_drv);
