// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB13, "cam_larb13", "top_cam", 0),
	GATE_CAM(CLK_CAM_LARB14, "cam_larb14", "top_cam", 1),
	GATE_CAM(CLK_CAM_MAIN_CAM, "cam_main_cam", "top_cam", 3),
	GATE_CAM(CLK_CAM_MAIN_CAMTG, "cam_main_camtg", "top_cam", 4),
	GATE_CAM(CLK_CAM_SENINF, "cam_seninf", "top_cam", 5),
	GATE_CAM(CLK_CAM_GCAMSVA, "cam_gcamsva", "top_cam", 6),
	GATE_CAM(CLK_CAM_GCAMSVB, "cam_gcamsvb", "top_cam", 7),
	GATE_CAM(CLK_CAM_GCAMSVC, "cam_gcamsvc", "top_cam", 8),
	GATE_CAM(CLK_CAM_SCAMSA, "cam_scamsa", "top_cam", 9),
	GATE_CAM(CLK_CAM_SCAMSB, "cam_scamsb", "top_cam", 10),
	GATE_CAM(CLK_CAM_CAMSV_TOP, "cam_camsv_top", "top_cam", 11),
	GATE_CAM(CLK_CAM_CAMSV_CQ, "cam_camsv_cq", "top_cam", 12),
	GATE_CAM(CLK_CAM_ADL, "cam_adl", "top_cam", 16),
	GATE_CAM(CLK_CAM_ASG, "cam_asg", "top_cam", 17),
	GATE_CAM(CLK_CAM_PDA, "cam_pda", "top_cam", 18),
	GATE_CAM(CLK_CAM_FAKE_ENG, "cam_fake_eng", "top_cam", 19),
	GATE_CAM(CLK_CAM_MAIN_MRAW0, "cam_main_mraw0", "top_cam", 20),
	GATE_CAM(CLK_CAM_MAIN_MRAW1, "cam_main_mraw1", "top_cam", 21),
	GATE_CAM(CLK_CAM_MAIN_MRAW2, "cam_main_mraw2", "top_cam", 22),
	GATE_CAM(CLK_CAM_MAIN_MRAW3, "cam_main_mraw3", "top_cam", 23),
	GATE_CAM(CLK_CAM_CAM2MM0_GALS, "cam_cam2mm0_gals", "top_cam", 24),
	GATE_CAM(CLK_CAM_CAM2MM1_GALS, "cam_cam2mm1_gals", "top_cam", 25),
	GATE_CAM(CLK_CAM_CAM2SYS_GALS, "cam_cam2sys_gals", "top_cam", 26),
};

static const struct mtk_gate cam_mraw_clks[] = {
	GATE_CAM(CLK_CAM_MRAW_LARBX, "cam_mraw_larbx", "top_cam", 0),
	GATE_CAM(CLK_CAM_MRAW_CAMTG, "cam_mraw_camtg", "top_cam", 2),
	GATE_CAM(CLK_CAM_MRAW_MRAW0, "cam_mraw_mraw0", "top_cam", 3),
	GATE_CAM(CLK_CAM_MRAW_MRAW1, "cam_mraw_mraw1", "top_cam", 4),
	GATE_CAM(CLK_CAM_MRAW_MRAW2, "cam_mraw_mraw2", "top_cam", 5),
	GATE_CAM(CLK_CAM_MRAW_MRAW3, "cam_mraw_mraw3", "top_cam", 6),
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

static const struct mtk_clk_desc cam_desc = {
	.clks = cam_clks,
	.num_clks = ARRAY_SIZE(cam_clks),
};

static const struct mtk_clk_desc cam_mraw_desc = {
	.clks = cam_mraw_clks,
	.num_clks = ARRAY_SIZE(cam_mraw_clks),
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

static const struct of_device_id of_match_clk_mt8195_cam[] = {
	{
		.compatible = "mediatek,mt8195-camsys",
		.data = &cam_desc,
	}, {
		.compatible = "mediatek,mt8195-camsys_mraw",
		.data = &cam_mraw_desc,
	}, {
		.compatible = "mediatek,mt8195-camsys_rawa",
		.data = &cam_rawa_desc,
	}, {
		.compatible = "mediatek,mt8195-camsys_rawb",
		.data = &cam_rawb_desc,
	}, {
		.compatible = "mediatek,mt8195-camsys_yuva",
		.data = &cam_yuva_desc,
	}, {
		.compatible = "mediatek,mt8195-camsys_yuvb",
		.data = &cam_yuvb_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-cam",
		.of_match_table = of_match_clk_mt8195_cam,
	},
};
builtin_platform_driver(clk_mt8195_cam_drv);
