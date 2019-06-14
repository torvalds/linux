// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt6797-clk.h>

#include "clk-mtk.h"
#include "clk-gate.h"

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
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
	GATE_IMG(CLK_IMG_FDVT, "img_fdvt", "mm_sel", 11),
	GATE_IMG(CLK_IMG_DPE, "img_dpe", "mm_sel", 10),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "mm_sel", 6),
	GATE_IMG(CLK_IMG_LARB6, "img_larb6", "mm_sel", 0),
};

static const struct of_device_id of_match_clk_mt6797_img[] = {
	{ .compatible = "mediatek,mt6797-imgsys", },
	{}
};

static int clk_mt6797_img_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_IMG_NR);

	mtk_clk_register_gates(node, img_clks, ARRAY_SIZE(img_clks),
			       clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt6797_img_drv = {
	.probe = clk_mt6797_img_probe,
	.driver = {
		.name = "clk-mt6797-img",
		.of_match_table = of_match_clk_mt6797_img,
	},
};

builtin_platform_driver(clk_mt6797_img_drv);
