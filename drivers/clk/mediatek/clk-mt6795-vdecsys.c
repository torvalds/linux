// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt6795-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"

#define GATE_VDEC(_id, _name, _parent, _regs)			\
		GATE_MTK(_id, _name, _parent, _regs, 0,		\
			 &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0000,
	.clr_ofs = 0x0004,
	.sta_ofs = 0x0000,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x000c,
	.sta_ofs = 0x0008,
};

static const struct mtk_gate vdec_clks[] = {
	GATE_VDEC(CLK_VDEC_CKEN, "vdec_cken", "vdec_sel", &vdec0_cg_regs),
	GATE_VDEC(CLK_VDEC_LARB_CKEN, "vdec_larb_cken", "mm_sel", &vdec1_cg_regs),
};

static const struct mtk_clk_desc vdec_desc = {
	.clks = vdec_clks,
	.num_clks = ARRAY_SIZE(vdec_clks),
};

static const struct of_device_id of_match_clk_mt6795_vdecsys[] = {
	{ .compatible = "mediatek,mt6795-vdecsys", .data = &vdec_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt6795_vdecsys_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6795-vdecsys",
		.of_match_table = of_match_clk_mt6795_vdecsys,
	},
};
module_platform_driver(clk_mt6795_vdecsys_drv);

MODULE_DESCRIPTION("MediaTek MT6795 vdecsys clocks driver");
MODULE_LICENSE("GPL");
