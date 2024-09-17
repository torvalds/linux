// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB13, "cam_larb13", "top_cam", 0),
	GATE_CAM(CLK_CAM_DFP_VAD, "cam_dfp_vad", "top_cam", 1),
	GATE_CAM(CLK_CAM_LARB14, "cam_larb14", "top_cam", 2),
	GATE_CAM(CLK_CAM, "cam", "top_cam", 6),
	GATE_CAM(CLK_CAMTG, "camtg", "top_cam", 7),
	GATE_CAM(CLK_CAM_SENINF, "cam_seninf", "top_cam", 8),
	GATE_CAM(CLK_CAMSV1, "camsv1", "top_cam", 10),
	GATE_CAM(CLK_CAMSV2, "camsv2", "top_cam", 11),
	GATE_CAM(CLK_CAMSV3, "camsv3", "top_cam", 12),
	GATE_CAM(CLK_CAM_CCU0, "cam_ccu0", "top_cam", 13),
	GATE_CAM(CLK_CAM_CCU1, "cam_ccu1", "top_cam", 14),
	GATE_CAM(CLK_CAM_MRAW0, "cam_mraw0", "top_cam", 15),
	GATE_CAM(CLK_CAM_FAKE_ENG, "cam_fake_eng", "top_cam", 17),
	GATE_CAM(CLK_CAM_CCU_GALS, "cam_ccu_gals", "top_cam", 18),
	GATE_CAM(CLK_CAM2MM_GALS, "cam2mm_gals", "top_cam", 19),
};

static const struct mtk_gate cam_rawa_clks[] = {
	GATE_CAM(CLK_CAM_RAWA_LARBX_RAWA, "cam_rawa_larbx_rawa", "top_cam", 0),
	GATE_CAM(CLK_CAM_RAWA, "cam_rawa", "top_cam", 1),
	GATE_CAM(CLK_CAM_RAWA_CAMTG_RAWA, "cam_rawa_camtg_rawa", "top_cam", 2),
};

static const struct mtk_gate cam_rawb_clks[] = {
	GATE_CAM(CLK_CAM_RAWB_LARBX_RAWB, "cam_rawb_larbx_rawb", "top_cam", 0),
	GATE_CAM(CLK_CAM_RAWB, "cam_rawb", "top_cam", 1),
	GATE_CAM(CLK_CAM_RAWB_CAMTG_RAWB, "cam_rawb_camtg_rawb", "top_cam", 2),
};

static const struct mtk_clk_desc cam_desc = {
	.clks = cam_clks,
	.num_clks = ARRAY_SIZE(cam_clks),
};

static const struct mtk_clk_desc cam_rawa_desc = {
	.clks = cam_rawa_clks,
	.num_clks = ARRAY_SIZE(cam_rawa_clks),
};

static const struct mtk_clk_desc cam_rawb_desc = {
	.clks = cam_rawb_clks,
	.num_clks = ARRAY_SIZE(cam_rawb_clks),
};

static const struct of_device_id of_match_clk_mt8186_cam[] = {
	{
		.compatible = "mediatek,mt8186-camsys",
		.data = &cam_desc,
	}, {
		.compatible = "mediatek,mt8186-camsys_rawa",
		.data = &cam_rawa_desc,
	}, {
		.compatible = "mediatek,mt8186-camsys_rawb",
		.data = &cam_rawb_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8186_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-cam",
		.of_match_table = of_match_clk_mt8186_cam,
	},
};
module_platform_driver(clk_mt8186_cam_drv);
MODULE_LICENSE("GPL");
