// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8183-clk.h>

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB5, "img_larb5", "img_sel", 0),
	GATE_IMG(CLK_IMG_LARB2, "img_larb2", "img_sel", 1),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "img_sel", 2),
	GATE_IMG(CLK_IMG_FDVT, "img_fdvt", "img_sel", 3),
	GATE_IMG(CLK_IMG_DPE, "img_dpe", "img_sel", 4),
	GATE_IMG(CLK_IMG_RSC, "img_rsc", "img_sel", 5),
	GATE_IMG(CLK_IMG_MFB, "img_mfb", "img_sel", 6),
	GATE_IMG(CLK_IMG_WPE_A, "img_wpe_a", "img_sel", 7),
	GATE_IMG(CLK_IMG_WPE_B, "img_wpe_b", "img_sel", 8),
	GATE_IMG(CLK_IMG_OWE, "img_owe", "img_sel", 9),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt8183_img[] = {
	{
		.compatible = "mediatek,mt8183-imgsys",
		.data = &img_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8183_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8183-img",
		.of_match_table = of_match_clk_mt8183_img,
	},
};
module_platform_driver(clk_mt8183_img_drv);
