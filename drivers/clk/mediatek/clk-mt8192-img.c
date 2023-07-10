// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8192-clk.h>

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB9, "img_larb9", "img1_sel", 0),
	GATE_IMG(CLK_IMG_LARB10, "img_larb10", "img1_sel", 1),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "img1_sel", 2),
	GATE_IMG(CLK_IMG_GALS, "img_gals", "img1_sel", 12),
};

static const struct mtk_gate img2_clks[] = {
	GATE_IMG(CLK_IMG2_LARB11, "img2_larb11", "img1_sel", 0),
	GATE_IMG(CLK_IMG2_LARB12, "img2_larb12", "img1_sel", 1),
	GATE_IMG(CLK_IMG2_MFB, "img2_mfb", "img1_sel", 6),
	GATE_IMG(CLK_IMG2_WPE, "img2_wpe", "img1_sel", 7),
	GATE_IMG(CLK_IMG2_MSS, "img2_mss", "img1_sel", 8),
	GATE_IMG(CLK_IMG2_GALS, "img2_gals", "img1_sel", 12),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct mtk_clk_desc img2_desc = {
	.clks = img2_clks,
	.num_clks = ARRAY_SIZE(img2_clks),
};

static const struct of_device_id of_match_clk_mt8192_img[] = {
	{
		.compatible = "mediatek,mt8192-imgsys",
		.data = &img_desc,
	}, {
		.compatible = "mediatek,mt8192-imgsys2",
		.data = &img2_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8192_img);

static struct platform_driver clk_mt8192_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8192-img",
		.of_match_table = of_match_clk_mt8192_img,
	},
};
module_platform_driver(clk_mt8192_img_drv);
MODULE_LICENSE("GPL");
