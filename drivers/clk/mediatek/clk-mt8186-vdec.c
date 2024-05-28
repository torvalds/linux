// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8186-clk.h>

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x4,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x190,
	.clr_ofs = 0x190,
	.sta_ofs = 0x190,
};

static const struct mtk_gate_regs vdec2_cg_regs = {
	.set_ofs = 0x200,
	.clr_ofs = 0x204,
	.sta_ofs = 0x200,
};

static const struct mtk_gate_regs vdec3_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x8,
};

#define GATE_VDEC0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec0_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_VDEC2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec2_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC3(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec3_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate vdec_clks[] = {
	/* VDEC0 */
	GATE_VDEC0(CLK_VDEC_CKEN, "vdec_cken", "top_vdec", 0),
	GATE_VDEC0(CLK_VDEC_ACTIVE, "vdec_active", "top_vdec", 4),
	GATE_VDEC0(CLK_VDEC_CKEN_ENG, "vdec_cken_eng", "top_vdec", 8),
	/* VDEC1 */
	GATE_VDEC1(CLK_VDEC_MINI_MDP_CKEN_CFG_RG, "vdec_mini_mdp_cken_cfg_rg", "top_vdec", 0),
	/* VDEC2 */
	GATE_VDEC2(CLK_VDEC_LAT_CKEN, "vdec_lat_cken", "top_vdec", 0),
	GATE_VDEC2(CLK_VDEC_LAT_ACTIVE, "vdec_lat_active", "top_vdec", 4),
	GATE_VDEC2(CLK_VDEC_LAT_CKEN_ENG, "vdec_lat_cken_eng", "top_vdec", 8),
	/* VDEC3 */
	GATE_VDEC3(CLK_VDEC_LARB1_CKEN, "vdec_larb1_cken", "top_vdec", 0),
};

static const struct mtk_clk_desc vdec_desc = {
	.clks = vdec_clks,
	.num_clks = ARRAY_SIZE(vdec_clks),
};

static const struct of_device_id of_match_clk_mt8186_vdec[] = {
	{
		.compatible = "mediatek,mt8186-vdecsys",
		.data = &vdec_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8186_vdec);

static struct platform_driver clk_mt8186_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-vdec",
		.of_match_table = of_match_clk_mt8186_vdec,
	},
};
module_platform_driver(clk_mt8186_vdec_drv);

MODULE_DESCRIPTION("MediaTek MT8186 Video Decoders clocks driver");
MODULE_LICENSE("GPL");
