// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mediatek,mt6735-vencsys.h>

#define VENC_CG_CON			0x00
#define VENC_CG_SET			0x04
#define VENC_CG_CLR			0x08

static struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = VENC_CG_SET,
	.clr_ofs = VENC_CG_CLR,
	.sta_ofs = VENC_CG_CON,
};

static const struct mtk_gate vencsys_gates[] = {
	GATE_MTK(CLK_VENC_SMI_LARB3, "smi_larb3", "mm_sel", &venc_cg_regs, 0, &mtk_clk_gate_ops_setclr_inv),
	GATE_MTK(CLK_VENC_VENC, "venc", "mm_sel", &venc_cg_regs, 4, &mtk_clk_gate_ops_setclr_inv),
	GATE_MTK(CLK_VENC_JPGENC, "jpgenc", "mm_sel", &venc_cg_regs, 8, &mtk_clk_gate_ops_setclr_inv),
	GATE_MTK(CLK_VENC_JPGDEC, "jpgdec", "mm_sel", &venc_cg_regs, 12, &mtk_clk_gate_ops_setclr_inv),
};

static const struct mtk_clk_desc vencsys_clks = {
	.clks = vencsys_gates,
	.num_clks = ARRAY_SIZE(vencsys_gates),
};

static const struct of_device_id of_match_mt6735_vencsys[] = {
	{ .compatible = "mediatek,mt6735-vencsys", .data = &vencsys_clks },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt6735_vencsys = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6735-vencsys",
		.of_match_table = of_match_mt6735_vencsys,
	},
};
module_platform_driver(clk_mt6735_vencsys);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Mediatek MT6735 vencsys clock driver");
MODULE_LICENSE("GPL");
