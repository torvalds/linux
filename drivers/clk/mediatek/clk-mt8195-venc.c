// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &venc_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc_clks[] = {
	GATE_VENC(CLK_VENC_LARB, "venc_larb", "top_venc", 0),
	GATE_VENC(CLK_VENC_VENC, "venc_venc", "top_venc", 4),
	GATE_VENC(CLK_VENC_JPGENC, "venc_jpgenc", "top_venc", 8),
	GATE_VENC(CLK_VENC_JPGDEC, "venc_jpgdec", "top_venc", 12),
	GATE_VENC(CLK_VENC_JPGDEC_C1, "venc_jpgdec_c1", "top_venc", 16),
	GATE_VENC(CLK_VENC_GALS, "venc_gals", "top_venc", 28),
};

static const struct mtk_gate venc_core1_clks[] = {
	GATE_VENC(CLK_VENC_CORE1_LARB, "venc_core1_larb", "top_venc", 0),
	GATE_VENC(CLK_VENC_CORE1_VENC, "venc_core1_venc", "top_venc", 4),
	GATE_VENC(CLK_VENC_CORE1_JPGENC, "venc_core1_jpgenc", "top_venc", 8),
	GATE_VENC(CLK_VENC_CORE1_JPGDEC, "venc_core1_jpgdec", "top_venc", 12),
	GATE_VENC(CLK_VENC_CORE1_JPGDEC_C1, "venc_core1_jpgdec_c1", "top_venc", 16),
	GATE_VENC(CLK_VENC_CORE1_GALS, "venc_core1_gals", "top_venc", 28),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct mtk_clk_desc venc_core1_desc = {
	.clks = venc_core1_clks,
	.num_clks = ARRAY_SIZE(venc_core1_clks),
};

static const struct of_device_id of_match_clk_mt8195_venc[] = {
	{
		.compatible = "mediatek,mt8195-vencsys",
		.data = &venc_desc,
	}, {
		.compatible = "mediatek,mt8195-vencsys_core1",
		.data = &venc_core1_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-venc",
		.of_match_table = of_match_clk_mt8195_venc,
	},
};
builtin_platform_driver(clk_mt8195_venc_drv);
