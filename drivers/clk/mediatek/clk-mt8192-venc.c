// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8192-clk.h>

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &venc_cg_regs, _shift, &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc_clks[] = {
	GATE_VENC(CLK_VENC_SET0_LARB, "venc_set0_larb", "venc_sel", 0),
	GATE_VENC(CLK_VENC_SET1_VENC, "venc_set1_venc", "venc_sel", 4),
	GATE_VENC(CLK_VENC_SET2_JPGENC, "venc_set2_jpgenc", "venc_sel", 8),
	GATE_VENC(CLK_VENC_SET5_GALS, "venc_set5_gals", "venc_sel", 28),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct of_device_id of_match_clk_mt8192_venc[] = {
	{
		.compatible = "mediatek,mt8192-vencsys",
		.data = &venc_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8192_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8192-venc",
		.of_match_table = of_match_clk_mt8192_venc,
	},
};

builtin_platform_driver(clk_mt8192_venc_drv);
