// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt6797-clk.h>

#include "clk-mtk.h"
#include "clk-gate.h"

static const struct mtk_gate_regs img_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_IMG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &img_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate img_clks[] = {
	GATE_IMG(CLK_IMG_FDVT, "img_fdvt", "mm_sel", 11),
	GATE_IMG(CLK_IMG_DPE, "img_dpe", "mm_sel", 10),
	GATE_IMG(CLK_IMG_DIP, "img_dip", "mm_sel", 6),
	GATE_IMG(CLK_IMG_LARB6, "img_larb6", "mm_sel", 0),
};

static const struct mtk_clk_desc img_desc = {
	.clks = img_clks,
	.num_clks = ARRAY_SIZE(img_clks),
};

static const struct of_device_id of_match_clk_mt6797_img[] = {
	{
		.compatible = "mediatek,mt6797-imgsys",
		.data = &img_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6797_img_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6797-img",
		.of_match_table = of_match_clk_mt6797_img,
	},
};
module_platform_driver(clk_mt6797_img_drv);
MODULE_LICENSE("GPL");
