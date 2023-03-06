// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs venc_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_VENC(_id, _name, _parent, _shift) \
		GATE_MTK(_id, _name, _parent, &venc_cg_regs, _shift, \
			 &mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate venc_clks[] = {
	/* VENC */
	GATE_VENC(CLK_VENC, "venc_fvenc_ck", "mm_sel", 4),
	GATE_VENC(CLK_VENC_JPGENC, "venc_jpgenc_ck", "mm_sel", 8),
};

static const struct mtk_clk_desc venc_desc = {
	.clks = venc_clks,
	.num_clks = ARRAY_SIZE(venc_clks),
};

static const struct of_device_id of_match_clk_mt8365_venc[] = {
	{
		.compatible = "mediatek,mt8365-vencsys",
		.data = &venc_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8365_venc_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8365-venc",
		.of_match_table = of_match_clk_mt8365_venc,
	},
};
module_platform_driver(clk_mt8365_venc_drv);
MODULE_LICENSE("GPL");
