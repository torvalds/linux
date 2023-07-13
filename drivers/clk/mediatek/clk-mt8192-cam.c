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

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB13, "cam_larb13", "cam_sel", 0),
	GATE_CAM(CLK_CAM_DFP_VAD, "cam_dfp_vad", "cam_sel", 1),
	GATE_CAM(CLK_CAM_LARB14, "cam_larb14", "cam_sel", 2),
	GATE_CAM(CLK_CAM_CAM, "cam_cam", "cam_sel", 6),
	GATE_CAM(CLK_CAM_CAMTG, "cam_camtg", "cam_sel", 7),
	GATE_CAM(CLK_CAM_SENINF, "cam_seninf", "cam_sel", 8),
	GATE_CAM(CLK_CAM_CAMSV0, "cam_camsv0", "cam_sel", 9),
	GATE_CAM(CLK_CAM_CAMSV1, "cam_camsv1", "cam_sel", 10),
	GATE_CAM(CLK_CAM_CAMSV2, "cam_camsv2", "cam_sel", 11),
	GATE_CAM(CLK_CAM_CAMSV3, "cam_camsv3", "cam_sel", 12),
	GATE_CAM(CLK_CAM_CCU0, "cam_ccu0", "cam_sel", 13),
	GATE_CAM(CLK_CAM_CCU1, "cam_ccu1", "cam_sel", 14),
	GATE_CAM(CLK_CAM_MRAW0, "cam_mraw0", "cam_sel", 15),
	GATE_CAM(CLK_CAM_FAKE_ENG, "cam_fake_eng", "cam_sel", 17),
	GATE_CAM(CLK_CAM_CCU_GALS, "cam_ccu_gals", "cam_sel", 18),
	GATE_CAM(CLK_CAM_CAM2MM_GALS, "cam2mm_gals", "cam_sel", 19),
};

static const struct mtk_gate cam_rawa_clks[] = {
	GATE_CAM(CLK_CAM_RAWA_LARBX, "cam_rawa_larbx", "cam_sel", 0),
	GATE_CAM(CLK_CAM_RAWA_CAM, "cam_rawa_cam", "cam_sel", 1),
	GATE_CAM(CLK_CAM_RAWA_CAMTG, "cam_rawa_camtg", "cam_sel", 2),
};

static const struct mtk_gate cam_rawb_clks[] = {
	GATE_CAM(CLK_CAM_RAWB_LARBX, "cam_rawb_larbx", "cam_sel", 0),
	GATE_CAM(CLK_CAM_RAWB_CAM, "cam_rawb_cam", "cam_sel", 1),
	GATE_CAM(CLK_CAM_RAWB_CAMTG, "cam_rawb_camtg", "cam_sel", 2),
};

static const struct mtk_gate cam_rawc_clks[] = {
	GATE_CAM(CLK_CAM_RAWC_LARBX, "cam_rawc_larbx", "cam_sel", 0),
	GATE_CAM(CLK_CAM_RAWC_CAM, "cam_rawc_cam", "cam_sel", 1),
	GATE_CAM(CLK_CAM_RAWC_CAMTG, "cam_rawc_camtg", "cam_sel", 2),
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

static const struct mtk_clk_desc cam_rawc_desc = {
	.clks = cam_rawc_clks,
	.num_clks = ARRAY_SIZE(cam_rawc_clks),
};

static const struct of_device_id of_match_clk_mt8192_cam[] = {
	{
		.compatible = "mediatek,mt8192-camsys",
		.data = &cam_desc,
	}, {
		.compatible = "mediatek,mt8192-camsys_rawa",
		.data = &cam_rawa_desc,
	}, {
		.compatible = "mediatek,mt8192-camsys_rawb",
		.data = &cam_rawb_desc,
	}, {
		.compatible = "mediatek,mt8192-camsys_rawc",
		.data = &cam_rawc_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8192_cam);

static struct platform_driver clk_mt8192_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8192-cam",
		.of_match_table = of_match_clk_mt8192_cam,
	},
};
module_platform_driver(clk_mt8192_cam_drv);
MODULE_LICENSE("GPL");
