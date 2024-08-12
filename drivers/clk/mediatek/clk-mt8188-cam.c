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

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_main_clks[] = {
	GATE_CAM(CLK_CAM_MAIN_LARB13, "cam_main_larb13", "top_cam", 0),
	GATE_CAM(CLK_CAM_MAIN_LARB14, "cam_main_larb14", "top_cam", 1),
	GATE_CAM(CLK_CAM_MAIN_CAM, "cam_main_cam", "top_cam", 2),
	GATE_CAM(CLK_CAM_MAIN_CAM_SUBA, "cam_main_cam_suba", "top_cam", 3),
	GATE_CAM(CLK_CAM_MAIN_CAM_SUBB, "cam_main_cam_subb", "top_cam", 4),
	GATE_CAM(CLK_CAM_MAIN_CAMTG, "cam_main_camtg", "top_cam", 7),
	GATE_CAM(CLK_CAM_MAIN_SENINF, "cam_main_seninf", "top_cam", 8),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVA, "cam_main_gcamsva", "top_cam", 9),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVB, "cam_main_gcamsvb", "top_cam", 10),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVC, "cam_main_gcamsvc", "top_cam", 11),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVD, "cam_main_gcamsvd", "top_cam", 12),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVE, "cam_main_gcamsve", "top_cam", 13),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVF, "cam_main_gcamsvf", "top_cam", 14),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVG, "cam_main_gcamsvg", "top_cam", 15),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVH, "cam_main_gcamsvh", "top_cam", 16),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVI, "cam_main_gcamsvi", "top_cam", 17),
	GATE_CAM(CLK_CAM_MAIN_GCAMSVJ, "cam_main_gcamsvj", "top_cam", 18),
	GATE_CAM(CLK_CAM_MAIN_CAMSV_TOP, "cam_main_camsv", "top_cam", 19),
	GATE_CAM(CLK_CAM_MAIN_CAMSV_CQ_A, "cam_main_camsv_cq_a", "top_cam", 20),
	GATE_CAM(CLK_CAM_MAIN_CAMSV_CQ_B, "cam_main_camsv_cq_b", "top_cam", 21),
	GATE_CAM(CLK_CAM_MAIN_CAMSV_CQ_C, "cam_main_camsv_cq_c", "top_cam", 22),
	GATE_CAM(CLK_CAM_MAIN_FAKE_ENG, "cam_main_fake_eng", "top_cam", 28),
	GATE_CAM(CLK_CAM_MAIN_CAM2MM0_GALS, "cam_main_cam2mm0_gals", "top_cam", 29),
	GATE_CAM(CLK_CAM_MAIN_CAM2MM1_GALS, "cam_main_cam2mm1_gals", "top_cam", 30),
	GATE_CAM(CLK_CAM_MAIN_CAM2SYS_GALS, "cam_main_cam2sys_gals", "top_cam", 31),
};

static const struct mtk_gate cam_rawa_clks[] = {
	GATE_CAM(CLK_CAM_RAWA_LARBX, "cam_rawa_larbx", "top_cam", 0),
	GATE_CAM(CLK_CAM_RAWA_CAM, "cam_rawa_cam", "top_cam", 1),
	GATE_CAM(CLK_CAM_RAWA_CAMTG, "cam_rawa_camtg", "top_cam", 2),
};

static const struct mtk_gate cam_rawb_clks[] = {
	GATE_CAM(CLK_CAM_RAWB_LARBX, "cam_rawb_larbx", "top_cam", 0),
	GATE_CAM(CLK_CAM_RAWB_CAM, "cam_rawb_cam", "top_cam", 1),
	GATE_CAM(CLK_CAM_RAWB_CAMTG, "cam_rawb_camtg", "top_cam", 2),
};

static const struct mtk_gate cam_yuva_clks[] = {
	GATE_CAM(CLK_CAM_YUVA_LARBX, "cam_yuva_larbx", "top_cam", 0),
	GATE_CAM(CLK_CAM_YUVA_CAM, "cam_yuva_cam", "top_cam", 1),
	GATE_CAM(CLK_CAM_YUVA_CAMTG, "cam_yuva_camtg", "top_cam", 2),
};

static const struct mtk_gate cam_yuvb_clks[] = {
	GATE_CAM(CLK_CAM_YUVB_LARBX, "cam_yuvb_larbx", "top_cam", 0),
	GATE_CAM(CLK_CAM_YUVB_CAM, "cam_yuvb_cam", "top_cam", 1),
	GATE_CAM(CLK_CAM_YUVB_CAMTG, "cam_yuvb_camtg", "top_cam", 2),
};

static const struct mtk_clk_desc cam_main_desc = {
	.clks = cam_main_clks,
	.num_clks = ARRAY_SIZE(cam_main_clks),
};

static const struct mtk_clk_desc cam_rawa_desc = {
	.clks = cam_rawa_clks,
	.num_clks = ARRAY_SIZE(cam_rawa_clks),
};

static const struct mtk_clk_desc cam_rawb_desc = {
	.clks = cam_rawb_clks,
	.num_clks = ARRAY_SIZE(cam_rawb_clks),
};

static const struct mtk_clk_desc cam_yuva_desc = {
	.clks = cam_yuva_clks,
	.num_clks = ARRAY_SIZE(cam_yuva_clks),
};

static const struct mtk_clk_desc cam_yuvb_desc = {
	.clks = cam_yuvb_clks,
	.num_clks = ARRAY_SIZE(cam_yuvb_clks),
};

static const struct of_device_id of_match_clk_mt8188_cam[] = {
	{ .compatible = "mediatek,mt8188-camsys", .data = &cam_main_desc },
	{ .compatible = "mediatek,mt8188-camsys-rawa", .data = &cam_rawa_desc },
	{ .compatible = "mediatek,mt8188-camsys-rawb", .data = &cam_rawb_desc },
	{ .compatible = "mediatek,mt8188-camsys-yuva", .data = &cam_yuva_desc },
	{ .compatible = "mediatek,mt8188-camsys-yuvb", .data = &cam_yuvb_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8188_cam);

static struct platform_driver clk_mt8188_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8188-cam",
		.of_match_table = of_match_clk_mt8188_cam,
	},
};
module_platform_driver(clk_mt8188_cam_drv);

MODULE_DESCRIPTION("MediaTek MT8188 Camera clocks driver");
MODULE_LICENSE("GPL");
