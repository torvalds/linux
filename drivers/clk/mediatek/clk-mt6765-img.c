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

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift) {		\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &img_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB2, "img_larb2", "mm_ck", 0),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "mm_ck", 2),
	GATE_IMG(CLK_IMG_FDVT, "img_fdvt", "mm_ck", 3),
	GATE_IMG(CLK_IMG_DPE, "img_dpe", "mm_ck", 4),
	GATE_IMG(CLK_IMG_RSC, "img_rsc", "mm_ck", 5),
};

static int clk_mt6765_img_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_IMG_NR_CLK);

	mtk_clk_register_gates(node, img_clks, ARRAY_SIZE(img_clks), clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	return r;
}

static const struct of_device_id of_match_clk_mt6765_img[] = {
	{ .compatible = "mediatek,mt6765-imgsys", },
	{}
};

static struct platform_driver clk_mt6765_img_drv = {
	.probe = clk_mt6765_img_probe,
	.driver = {
		.name = "clk-mt6765-img",
		.of_match_table = of_match_clk_mt6765_img,
	},
};

builtin_platform_driver(clk_mt6765_img_drv);
