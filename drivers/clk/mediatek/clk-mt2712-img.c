// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Weiyi Lu <weiyi.lu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2712-clk.h>

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &img_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_SMI_LARB2, "img_smi_larb2", "mm_sel", 0),
	GATE_IMG(CLK_IMG_SENINF_SCAM_EN, "img_scam_en", "csi0", 3),
	GATE_IMG(CLK_IMG_SENINF_CAM_EN, "img_cam_en", "mm_sel", 8),
	GATE_IMG(CLK_IMG_CAM_SV_EN, "img_cam_sv_en", "mm_sel", 9),
	GATE_IMG(CLK_IMG_CAM_SV1_EN, "img_cam_sv1_en", "mm_sel", 10),
	GATE_IMG(CLK_IMG_CAM_SV2_EN, "img_cam_sv2_en", "mm_sel", 11),
};

static int clk_mt2712_img_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_IMG_NR_CLK);

	mtk_clk_register_gates(node, img_clks, ARRAY_SIZE(img_clks),
			clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r != 0)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

	return r;
}

static const struct of_device_id of_match_clk_mt2712_img[] = {
	{ .compatible = "mediatek,mt2712-imgsys", },
	{}
};

static struct platform_driver clk_mt2712_img_drv = {
	.probe = clk_mt2712_img_probe,
	.driver = {
		.name = "clk-mt2712-img",
		.of_match_table = of_match_clk_mt2712_img,
	},
};

builtin_platform_driver(clk_mt2712_img_drv);
