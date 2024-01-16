// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6765-clk.h>

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB3, "cam_larb3", "mm_ck", 0),
	GATE_CAM(CLK_CAM_DFP_VAD, "cam_dfp_vad", "mm_ck", 1),
	GATE_CAM(CLK_CAM, "cam", "mm_ck", 6),
	GATE_CAM(CLK_CAMTG, "camtg", "mm_ck", 7),
	GATE_CAM(CLK_CAM_SENINF, "cam_seninf", "mm_ck", 8),
	GATE_CAM(CLK_CAMSV0, "camsv0", "mm_ck", 9),
	GATE_CAM(CLK_CAMSV1, "camsv1", "mm_ck", 10),
	GATE_CAM(CLK_CAMSV2, "camsv2", "mm_ck", 11),
	GATE_CAM(CLK_CAM_CCU, "cam_ccu", "mm_ck", 12),
};

static const struct mtk_clk_desc cam_desc = {
	.clks = cam_clks,
	.num_clks = ARRAY_SIZE(cam_clks),
};

static const struct of_device_id of_match_clk_mt6765_cam[] = {
	{
		.compatible = "mediatek,mt6765-camsys",
		.data = &cam_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6765_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6765-cam",
		.of_match_table = of_match_clk_mt6765_cam,
	},
};

builtin_platform_driver(clk_mt6765_cam_drv);
