// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2701-clk.h>

#define GATE_G3D(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &g3d_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate_regs g3d_cg_regs = {
	.sta_ofs = 0x0,
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
};

static const struct mtk_gate g3d_clks[] = {
	GATE_DUMMY(CLK_DUMMY, "g3d_dummy"),
	GATE_G3D(CLK_G3DSYS_CORE, "g3d_core", "mfg_sel", 0),
};

static u16 rst_ofs[] = { 0xc, };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(rst_ofs),
};

static const struct mtk_clk_desc g3d_desc = {
	.clks = g3d_clks,
	.num_clks = ARRAY_SIZE(g3d_clks),
	.rst_desc = &clk_rst_desc,
};

static const struct of_device_id of_match_clk_mt2701_g3d[] = {
	{ .compatible = "mediatek,mt2701-g3dsys", .data = &g3d_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt2701_g3d);

static struct platform_driver clk_mt2701_g3d_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2701-g3d",
		.of_match_table = of_match_clk_mt2701_g3d,
	},
};
module_platform_driver(clk_mt2701_g3d_drv);

MODULE_DESCRIPTION("MediaTek MT2701 GPU g3d clocks driver");
MODULE_LICENSE("GPL");
