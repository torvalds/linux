// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Garmin Chang <garmin.chang@mediatek.com>
 */

#include <dt-bindings/clock/mediatek,mt8188-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs imgsys_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMGSYS(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &imgsys_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate imgsys_main_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS_MAIN_LARB9, "imgsys_main_larb9", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_TRAW0, "imgsys_main_traw0", "top_img", 1),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_TRAW1, "imgsys_main_traw1", "top_img", 2),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_VCORE_GALS, "imgsys_main_vcore_gals", "top_img", 3),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_DIP0, "imgsys_main_dip0", "top_img", 8),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_WPE0, "imgsys_main_wpe0", "top_img", 9),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_IPE, "imgsys_main_ipe", "top_img", 10),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_WPE1, "imgsys_main_wpe1", "top_img", 12),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_WPE2, "imgsys_main_wpe2", "top_img", 13),
	GATE_IMGSYS(CLK_IMGSYS_MAIN_GALS, "imgsys_main_gals", "top_img", 31),
};

static const struct mtk_gate imgsys_wpe1_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS_WPE1_LARB11, "imgsys_wpe1_larb11", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS_WPE1, "imgsys_wpe1", "top_img", 1),
};

static const struct mtk_gate imgsys_wpe2_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS_WPE2_LARB11, "imgsys_wpe2_larb11", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS_WPE2, "imgsys_wpe2", "top_img", 1),
};

static const struct mtk_gate imgsys_wpe3_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS_WPE3_LARB11, "imgsys_wpe3_larb11", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS_WPE3, "imgsys_wpe3", "top_img", 1),
};

static const struct mtk_gate imgsys1_dip_top_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS1_DIP_TOP_LARB10, "imgsys1_dip_larb10", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS1_DIP_TOP_DIP_TOP, "imgsys1_dip_dip_top", "top_img", 1),
};

static const struct mtk_gate imgsys1_dip_nr_clks[] = {
	GATE_IMGSYS(CLK_IMGSYS1_DIP_NR_LARB15, "imgsys1_dip_nr_larb15", "top_img", 0),
	GATE_IMGSYS(CLK_IMGSYS1_DIP_NR_DIP_NR, "imgsys1_dip_nr_dip_nr", "top_img", 1),
};

static const struct mtk_clk_desc imgsys_main_desc = {
	.clks = imgsys_main_clks,
	.num_clks = ARRAY_SIZE(imgsys_main_clks),
};

static const struct mtk_clk_desc imgsys_wpe1_desc = {
	.clks = imgsys_wpe1_clks,
	.num_clks = ARRAY_SIZE(imgsys_wpe1_clks),
};

static const struct mtk_clk_desc imgsys_wpe2_desc = {
	.clks = imgsys_wpe2_clks,
	.num_clks = ARRAY_SIZE(imgsys_wpe2_clks),
};

static const struct mtk_clk_desc imgsys_wpe3_desc = {
	.clks = imgsys_wpe3_clks,
	.num_clks = ARRAY_SIZE(imgsys_wpe3_clks),
};

static const struct mtk_clk_desc imgsys1_dip_top_desc = {
	.clks = imgsys1_dip_top_clks,
	.num_clks = ARRAY_SIZE(imgsys1_dip_top_clks),
};

static const struct mtk_clk_desc imgsys1_dip_nr_desc = {
	.clks = imgsys1_dip_nr_clks,
	.num_clks = ARRAY_SIZE(imgsys1_dip_nr_clks),
};

static const struct of_device_id of_match_clk_mt8188_imgsys_main[] = {
	{ .compatible = "mediatek,mt8188-imgsys", .data = &imgsys_main_desc },
	{ .compatible = "mediatek,mt8188-imgsys-wpe1", .data = &imgsys_wpe1_desc },
	{ .compatible = "mediatek,mt8188-imgsys-wpe2", .data = &imgsys_wpe2_desc },
	{ .compatible = "mediatek,mt8188-imgsys-wpe3", .data = &imgsys_wpe3_desc },
	{ .compatible = "mediatek,mt8188-imgsys1-dip-top", .data = &imgsys1_dip_top_desc },
	{ .compatible = "mediatek,mt8188-imgsys1-dip-nr", .data = &imgsys1_dip_nr_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8188_imgsys_main);

static struct platform_driver clk_mt8188_imgsys_main_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8188-imgsys_main",
		.of_match_table = of_match_clk_mt8188_imgsys_main,
	},
};
module_platform_driver(clk_mt8188_imgsys_main_drv);

MODULE_DESCRIPTION("MediaTek MT8188 imgsys clocks driver");
MODULE_LICENSE("GPL");
