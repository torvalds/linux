// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Kevin-CW Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6797-clk.h>

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

#define GATE_VDEC0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &vdec0_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &vdec1_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate vdec_clks[] = {
	GATE_VDEC0(CLK_VDEC_CKEN_ENG, "vdec_cken_eng", "vdec_sel", 8),
	GATE_VDEC0(CLK_VDEC_ACTIVE, "vdec_active", "vdec_sel", 4),
	GATE_VDEC0(CLK_VDEC_CKEN, "vdec_cken", "vdec_sel", 0),
	GATE_VDEC1(CLK_VDEC_LARB1_CKEN, "vdec_larb1_cken", "mm_sel", 0),
};

static const struct mtk_clk_desc vdec_desc = {
	.clks = vdec_clks,
	.num_clks = ARRAY_SIZE(vdec_clks),
};

static const struct of_device_id of_match_clk_mt6797_vdec[] = {
	{
		.compatible = "mediatek,mt6797-vdecsys",
		.data = &vdec_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6797_vdec);

static struct platform_driver clk_mt6797_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6797-vdec",
		.of_match_table = of_match_clk_mt6797_vdec,
	},
};
module_platform_driver(clk_mt6797_vdec_drv);

MODULE_DESCRIPTION("MediaTek MT6797 Video Decoders clocks driver");
MODULE_LICENSE("GPL");
