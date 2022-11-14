// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Weiyi Lu <weiyi.lu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2712-clk.h>

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &img_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_SMI_LARB2, "img_smi_larb2", "mm_sel", 0),
	GATE_IMG(CLK_IMG_SENINF_SCAM_EN, "img_scam_en", "csi0", 3),
	GATE_IMG(CLK_IMG_SENINF_CAM_EN, "img_cam_en", "mm_sel", 8),
	GATE_IMG(CLK_IMG_CAM_SV_EN, "img_cam_sv_en", "mm_sel", 9),
	GATE_IMG(CLK_IMG_CAM_SV1_EN, "img_cam_sv1_en", "mm_sel", 10),
	GATE_IMG(CLK_IMG_CAM_SV2_EN, "img_cam_sv2_en", "mm_sel", 11),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt2712_img[] = {
	{
		.compatible = "mediatek,mt2712-imgsys",
		.data = &img_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt2712_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712-img",
		.of_match_table = of_match_clk_mt2712_img,
	},
};

builtin_platform_driver(clk_mt2712_img_drv);
