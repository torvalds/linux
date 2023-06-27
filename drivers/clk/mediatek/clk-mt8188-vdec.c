// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Garmin Chang <garmin.chang@mediatek.com>
 */

#include <dt-bindings/clock/mediatek,mt8188-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x4,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x200,
	.clr_ofs = 0x204,
	.sta_ofs = 0x200,
};

static const struct mtk_gate_regs vdec2_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0xc,
	.sta_ofs = 0x8,
};

#define GATE_VDEC0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec0_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec1_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

#define GATE_VDEC2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &vdec2_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate vdec1_clks[] = {
	/* VDEC1_0 */
	GATE_VDEC0(CLK_VDEC1_SOC_VDEC, "vdec1_soc_vdec", "top_vdec", 0),
	GATE_VDEC0(CLK_VDEC1_SOC_VDEC_ACTIVE, "vdec1_soc_vdec_active", "top_vdec", 4),
	GATE_VDEC0(CLK_VDEC1_SOC_VDEC_ENG, "vdec1_soc_vdec_eng", "top_vdec", 8),
	/* VDEC1_1 */
	GATE_VDEC1(CLK_VDEC1_SOC_LAT, "vdec1_soc_lat", "top_vdec", 0),
	GATE_VDEC1(CLK_VDEC1_SOC_LAT_ACTIVE, "vdec1_soc_lat_active", "top_vdec", 4),
	GATE_VDEC1(CLK_VDEC1_SOC_LAT_ENG, "vdec1_soc_lat_eng", "top_vdec", 8),
	/* VDEC1_2 */
	GATE_VDEC2(CLK_VDEC1_SOC_LARB1, "vdec1_soc_larb1", "top_vdec", 0),
};

static const struct mtk_gate vdec2_clks[] = {
	/* VDEC2_0 */
	GATE_VDEC0(CLK_VDEC2_VDEC, "vdec2_vdec", "top_vdec", 0),
	GATE_VDEC0(CLK_VDEC2_VDEC_ACTIVE, "vdec2_vdec_active", "top_vdec", 4),
	GATE_VDEC0(CLK_VDEC2_VDEC_ENG, "vdec2_vdec_eng", "top_vdec", 8),
	/* VDEC2_1 */
	GATE_VDEC1(CLK_VDEC2_LAT, "vdec2_lat", "top_vdec", 0),
	/* VDEC2_2 */
	GATE_VDEC2(CLK_VDEC2_LARB1, "vdec2_larb1", "top_vdec", 0),
};

static const struct mtk_clk_desc vdec1_desc = {
	.clks = vdec1_clks,
	.num_clks = ARRAY_SIZE(vdec1_clks),
};

static const struct mtk_clk_desc vdec2_desc = {
	.clks = vdec2_clks,
	.num_clks = ARRAY_SIZE(vdec2_clks),
};

static const struct of_device_id of_match_clk_mt8188_vdec[] = {
	{ .compatible = "mediatek,mt8188-vdecsys-soc", .data = &vdec1_desc },
	{ .compatible = "mediatek,mt8188-vdecsys", .data = &vdec2_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8188_vdec);

static struct platform_driver clk_mt8188_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8188-vdec",
		.of_match_table = of_match_clk_mt8188_vdec,
	},
};

module_platform_driver(clk_mt8188_vdec_drv);
MODULE_LICENSE("GPL");
