// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Garmin Chang <garmin.chang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/mediatek,mt8188-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs venc1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &venc1_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc1_clks[] = {
	GATE_VENC1(CLK_VENC1_LARB, "venc1_larb", "top_venc", 0),
	GATE_VENC1(CLK_VENC1_VENC, "venc1_venc", "top_venc", 4),
	GATE_VENC1(CLK_VENC1_JPGENC, "venc1_jpgenc", "top_venc", 8),
	GATE_VENC1(CLK_VENC1_JPGDEC, "venc1_jpgdec", "top_venc", 12),
	GATE_VENC1(CLK_VENC1_JPGDEC_C1, "venc1_jpgdec_c1", "top_venc", 16),
	GATE_VENC1(CLK_VENC1_GALS, "venc1_gals", "top_venc", 28),
	GATE_VENC1(CLK_VENC1_GALS_SRAM, "venc1_gals_sram", "top_venc", 31),
};

static const struct mtk_clk_desc venc1_desc = {
	.clks = venc1_clks,
	.num_clks = ARRAY_SIZE(venc1_clks),
};

static const struct of_device_id of_match_clk_mt8188_venc1[] = {
	{ .compatible = "mediatek,mt8188-vencsys", .data = &venc1_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8188_venc1);

static struct platform_driver clk_mt8188_venc1_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8188-venc1",
		.of_match_table = of_match_clk_mt8188_venc1,
	},
};
module_platform_driver(clk_mt8188_venc1_drv);

MODULE_DESCRIPTION("MediaTek MT8188 Video Encoders clocks driver");
MODULE_LICENSE("GPL");
