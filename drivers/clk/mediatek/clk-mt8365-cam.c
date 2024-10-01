// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_CAM(_id, _name, _parent, _shift) \
		GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift, \
			 &mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB2, "cam_larb2", "mm_sel", 0),
	GATE_CAM(CLK_CAM, "cam", "mm_sel", 6),
	GATE_CAM(CLK_CAMTG, "camtg", "mm_sel", 7),
	GATE_CAM(CLK_CAM_SENIF, "cam_senif", "mm_sel", 8),
	GATE_CAM(CLK_CAMSV0, "camsv0", "mm_sel", 9),
	GATE_CAM(CLK_CAMSV1, "camsv1", "mm_sel", 10),
	GATE_CAM(CLK_CAM_FDVT, "cam_fdvt", "mm_sel", 11),
	GATE_CAM(CLK_CAM_WPE, "cam_wpe", "mm_sel", 12),
};

static const struct mtk_clk_desc cam_desc = {
	.clks = cam_clks,
	.num_clks = ARRAY_SIZE(cam_clks),
};

static const struct of_device_id of_match_clk_mt8365_cam[] = {
	{
		.compatible = "mediatek,mt8365-imgsys",
		.data = &cam_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8365_cam);

static struct platform_driver clk_mt8365_cam_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8365-cam",
		.of_match_table = of_match_clk_mt8365_cam,
	},
};
module_platform_driver(clk_mt8365_cam_drv);

MODULE_DESCRIPTION("MediaTek MT8365 Camera clocks driver");
MODULE_LICENSE("GPL");
