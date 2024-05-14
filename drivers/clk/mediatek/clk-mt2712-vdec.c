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

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x4,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x8,
};

#define GATE_VDEC0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &vdec0_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &vdec1_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate vdec_clks[] = {
	/* VDEC0 */
	GATE_VDEC0(CLK_VDEC_CKEN, "vdec_cken", "vdec_sel", 0),
	/* VDEC1 */
	GATE_VDEC1(CLK_VDEC_LARB1_CKEN, "vdec_larb1_cken", "vdec_sel", 0),
	GATE_VDEC1(CLK_VDEC_IMGRZ_CKEN, "vdec_imgrz_cken", "vdec_sel", 1),
};

static const struct mtk_clk_desc vdec_desc = {
	.clks = vdec_clks,
	.num_clks = ARRAY_SIZE(vdec_clks),
};

static const struct of_device_id of_match_clk_mt2712_vdec[] = {
	{
		.compatible = "mediatek,mt2712-vdecsys",
		.data = &vdec_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt2712_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712-vdec",
		.of_match_table = of_match_clk_mt2712_vdec,
	},
};
module_platform_driver(clk_mt2712_vdec_drv);
MODULE_LICENSE("GPL");
