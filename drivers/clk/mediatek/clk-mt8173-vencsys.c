// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &venc_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc_clks[] = {
	GATE_DUMMY(CLK_DUMMY, "venc_dummy"),
	GATE_VENC(CLK_VENC_CKE0, "venc_cke0", "mm_sel", 0),
	GATE_VENC(CLK_VENC_CKE1, "venc_cke1", "venc_sel", 4),
	GATE_VENC(CLK_VENC_CKE2, "venc_cke2", "venc_sel", 8),
	GATE_VENC(CLK_VENC_CKE3, "venc_cke3", "venc_sel", 12),
};

static const struct mtk_gate venclt_clks[] = {
	GATE_DUMMY(CLK_DUMMY, "venclt_dummy"),
	GATE_VENC(CLK_VENCLT_CKE0, "venclt_cke0", "mm_sel", 0),
	GATE_VENC(CLK_VENCLT_CKE1, "venclt_cke1", "venclt_sel", 4),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct mtk_clk_desc venc_lt_desc = {
	.clks = venclt_clks,
	.num_clks = ARRAY_SIZE(venclt_clks),
};

static const struct of_device_id of_match_clk_mt8173_vencsys[] = {
	{ .compatible = "mediatek,mt8173-vencsys", .data = &venc_desc },
	{ .compatible = "mediatek,mt8173-vencltsys", .data = &venc_lt_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt8173_vencsys_drv = {
	.driver = {
		.name = "clk-mt8173-vencsys",
		.of_match_table = of_match_clk_mt8173_vencsys,
	},
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt8173_vencsys_drv);

MODULE_DESCRIPTION("MediaTek MT8173 vencsys clocks driver");
MODULE_LICENSE("GPL");
