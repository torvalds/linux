// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6765-clk.h>

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
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
	GATE_VENC(CLK_VENC_SET0_LARB, "venc_set0_larb", "mm_ck", 0),
	GATE_VENC(CLK_VENC_SET1_VENC, "venc_set1_venc", "mm_ck", 4),
	GATE_VENC(CLK_VENC_SET2_JPGENC, "jpgenc", "mm_ck", 8),
	GATE_VENC(CLK_VENC_SET3_VDEC, "venc_set3_vdec", "mm_ck", 12),
};

static int clk_mt6765_vcodec_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_VENC_NR_CLK);

	mtk_clk_register_gates(node, venc_clks,
			       ARRAY_SIZE(venc_clks), clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	return r;
}

static const struct of_device_id of_match_clk_mt6765_vcodec[] = {
	{ .compatible = "mediatek,mt6765-vcodecsys", },
	{}
};

static struct platform_driver clk_mt6765_vcodec_drv = {
	.probe = clk_mt6765_vcodec_probe,
	.driver = {
		.name = "clk-mt6765-vcodec",
		.of_match_table = of_match_clk_mt6765_vcodec,
	},
};

builtin_platform_driver(clk_mt6765_vcodec_drv);
