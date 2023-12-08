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

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_LARB2, "img_larb2", "mm_ck", 0),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "mm_ck", 2),
	GATE_IMG(CLK_IMG_FDVT, "img_fdvt", "mm_ck", 3),
	GATE_IMG(CLK_IMG_DPE, "img_dpe", "mm_ck", 4),
	GATE_IMG(CLK_IMG_RSC, "img_rsc", "mm_ck", 5),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt6765_img[] = {
	{
		.compatible = "mediatek,mt6765-imgsys",
		.data = &img_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6765_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6765-img",
		.of_match_table = of_match_clk_mt6765_img,
	},
};

builtin_platform_driver(clk_mt6765_img_drv);
