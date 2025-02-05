// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img1_clks[] = {
	GATE_IMG(CLK_IMG1_LARB9_IMG1, "img1_larb9_img1", "top_img1", 0),
	GATE_IMG(CLK_IMG1_LARB10_IMG1, "img1_larb10_img1", "top_img1", 1),
	GATE_IMG(CLK_IMG1_DIP, "img1_dip", "top_img1", 2),
	GATE_IMG(CLK_IMG1_GALS_IMG1, "img1_gals_img1", "top_img1", 12),
};

static const struct mtk_gate img2_clks[] = {
	GATE_IMG(CLK_IMG2_LARB9_IMG2, "img2_larb9_img2", "top_img1", 0),
	GATE_IMG(CLK_IMG2_LARB10_IMG2, "img2_larb10_img2", "top_img1", 1),
	GATE_IMG(CLK_IMG2_MFB, "img2_mfb", "top_img1", 6),
	GATE_IMG(CLK_IMG2_WPE, "img2_wpe", "top_img1", 7),
	GATE_IMG(CLK_IMG2_MSS, "img2_mss", "top_img1", 8),
	GATE_IMG(CLK_IMG2_GALS_IMG2, "img2_gals_img2", "top_img1", 12),
};

static const struct mtk_clk_desc img1_desc = {
	.clks = img1_clks,
	.num_clks = ARRAY_SIZE(img1_clks),
};

static const struct mtk_clk_desc img2_desc = {
	.clks = img2_clks,
	.num_clks = ARRAY_SIZE(img2_clks),
};

static const struct of_device_id of_match_clk_mt8186_img[] = {
	{
		.compatible = "mediatek,mt8186-imgsys1",
		.data = &img1_desc,
	}, {
		.compatible = "mediatek,mt8186-imgsys2",
		.data = &img2_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8186_img);

static struct platform_driver clk_mt8186_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-img",
		.of_match_table = of_match_clk_mt8186_img,
	},
};
module_platform_driver(clk_mt8186_img_drv);

MODULE_DESCRIPTION("MediaTek MT8186 imgsys clocks driver");
MODULE_LICENSE("GPL");
