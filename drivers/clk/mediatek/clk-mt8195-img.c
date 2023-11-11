// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB9, "img_larb9", "top_img", 0),
	GATE_IMG(CLK_IMG_TRAW0, "img_traw0", "top_img", 1),
	GATE_IMG(CLK_IMG_TRAW1, "img_traw1", "top_img", 2),
	GATE_IMG(CLK_IMG_TRAW2, "img_traw2", "top_img", 3),
	GATE_IMG(CLK_IMG_TRAW3, "img_traw3", "top_img", 4),
	GATE_IMG(CLK_IMG_DIP0, "img_dip0", "top_img", 8),
	GATE_IMG(CLK_IMG_WPE0, "img_wpe0", "top_img", 9),
	GATE_IMG(CLK_IMG_IPE, "img_ipe", "top_img", 10),
	GATE_IMG(CLK_IMG_DIP1, "img_dip1", "top_img", 11),
	GATE_IMG(CLK_IMG_WPE1, "img_wpe1", "top_img", 12),
	GATE_IMG(CLK_IMG_GALS, "img_gals", "top_img", 31),
};

static const struct mtk_gate img1_dip_top_clks[] = {
	GATE_IMG(CLK_IMG1_DIP_TOP_LARB10, "img1_dip_top_larb10", "top_img", 0),
	GATE_IMG(CLK_IMG1_DIP_TOP_DIP_TOP, "img1_dip_top_dip_top", "top_img", 1),
};

static const struct mtk_gate img1_dip_nr_clks[] = {
	GATE_IMG(CLK_IMG1_DIP_NR_RESERVE, "img1_dip_nr_reserve", "top_img", 0),
	GATE_IMG(CLK_IMG1_DIP_NR_DIP_NR, "img1_dip_nr_dip_nr", "top_img", 1),
};

static const struct mtk_gate img1_wpe_clks[] = {
	GATE_IMG(CLK_IMG1_WPE_LARB11, "img1_wpe_larb11", "top_img", 0),
	GATE_IMG(CLK_IMG1_WPE_WPE, "img1_wpe_wpe", "top_img", 1),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct mtk_clk_desc img1_dip_top_desc = {
	.clks = img1_dip_top_clks,
	.num_clks = ARRAY_SIZE(img1_dip_top_clks),
};

static const struct mtk_clk_desc img1_dip_nr_desc = {
	.clks = img1_dip_nr_clks,
	.num_clks = ARRAY_SIZE(img1_dip_nr_clks),
};

static const struct mtk_clk_desc img1_wpe_desc = {
	.clks = img1_wpe_clks,
	.num_clks = ARRAY_SIZE(img1_wpe_clks),
};

static const struct of_device_id of_match_clk_mt8195_img[] = {
	{
		.compatible = "mediatek,mt8195-imgsys",
		.data = &img_desc,
	}, {
		.compatible = "mediatek,mt8195-imgsys1_dip_top",
		.data = &img1_dip_top_desc,
	}, {
		.compatible = "mediatek,mt8195-imgsys1_dip_nr",
		.data = &img1_dip_nr_desc,
	}, {
		.compatible = "mediatek,mt8195-imgsys1_wpe",
		.data = &img1_wpe_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-img",
		.of_match_table = of_match_clk_mt8195_img,
	},
};
builtin_platform_driver(clk_mt8195_img_drv);
