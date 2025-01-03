// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mediatek,mt6735-imgsys.h>

#define IMG_CG_CON			0x00
#define IMG_CG_SET			0x04
#define IMG_CG_CLR			0x08

static struct mtk_gate_regs imgsys_cg_regs = {
	.set_ofs = IMG_CG_SET,
	.clr_ofs = IMG_CG_CLR,
	.sta_ofs = IMG_CG_CON,
};

static const struct mtk_gate imgsys_gates[] = {
	GATE_MTK(CLK_IMG_SMI_LARB2, "smi_larb2", "mm_sel", &imgsys_cg_regs, 0, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_CAM_SMI, "cam_smi", "mm_sel", &imgsys_cg_regs, 5, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_CAM_CAM, "cam_cam", "mm_sel", &imgsys_cg_regs, 6, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_SEN_TG, "sen_tg", "mm_sel", &imgsys_cg_regs, 7, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_SEN_CAM, "sen_cam", "mm_sel", &imgsys_cg_regs, 8, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_CAM_SV, "cam_sv", "mm_sel", &imgsys_cg_regs, 9, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_SUFOD, "sufod", "mm_sel", &imgsys_cg_regs, 10, &mtk_clk_gate_ops_setclr),
	GATE_MTK(CLK_IMG_FD, "fd", "mm_sel", &imgsys_cg_regs, 11, &mtk_clk_gate_ops_setclr),
};

static const struct mtk_clk_desc imgsys_clks = {
	.clks = imgsys_gates,
	.num_clks = ARRAY_SIZE(imgsys_gates),
};

static const struct of_device_id of_match_mt6735_imgsys[] = {
	{ .compatible = "mediatek,mt6735-imgsys", .data = &imgsys_clks },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt6735_imgsys = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6735-imgsys",
		.of_match_table = of_match_mt6735_imgsys,
	},
};
module_platform_driver(clk_mt6735_imgsys);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("MediaTek MT6735 imgsys clock driver");
MODULE_LICENSE("GPL");
