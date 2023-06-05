// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

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
	GATE_VENC(CLK_VENC_CKE0_LARB, "venc_cke0_larb", "top_venc", 0),
	GATE_VENC(CLK_VENC_CKE1_VENC, "venc_cke1_venc", "top_venc", 4),
	GATE_VENC(CLK_VENC_CKE2_JPGENC, "venc_cke2_jpgenc", "top_venc", 8),
	GATE_VENC(CLK_VENC_CKE5_GALS, "venc_cke5_gals", "top_venc", 28),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct of_device_id of_match_clk_mt8186_venc[] = {
	{
		.compatible = "mediatek,mt8186-vencsys",
		.data = &venc_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8186_venc);

static struct platform_driver clk_mt8186_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-venc",
		.of_match_table = of_match_clk_mt8186_venc,
	},
};
module_platform_driver(clk_mt8186_venc_drv);
MODULE_LICENSE("GPL");
