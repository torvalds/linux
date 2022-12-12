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

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &venc_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr_inv,	\
	}

static const struct mtk_gate venc_clks[] = {
	GATE_VENC(CLK_VENC_SMI_COMMON_CON, "venc_smi", "mm_sel", 0),
	GATE_VENC(CLK_VENC_VENC, "venc_venc", "venc_sel", 4),
	GATE_VENC(CLK_VENC_SMI_LARB6, "venc_smi_larb6", "jpgdec_sel", 12),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct of_device_id of_match_clk_mt2712_venc[] = {
	{
		.compatible = "mediatek,mt2712-vencsys",
		.data = &venc_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt2712_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712-venc",
		.of_match_table = of_match_clk_mt2712_venc,
	},
};

builtin_platform_driver(clk_mt2712_venc_drv);
