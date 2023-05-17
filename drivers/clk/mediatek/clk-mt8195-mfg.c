// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs mfg_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_MFG(_id, _name, _parent, _shift)			\
	GATE_MTK_FLAGS(_id, _name, _parent, &mfg_cg_regs,	\
		       _shift, &mtk_clk_gate_ops_setclr,	\
		       CLK_SET_RATE_PARENT)

static const struct mtk_gate mfg_clks[] = {
	GATE_MFG(CLK_MFG_BG3D, "mfg_bg3d", "mfg_ck_fast_ref", 0),
};

static const struct mtk_clk_desc mfg_desc = {
	.clks = mfg_clks,
	.num_clks = ARRAY_SIZE(mfg_clks),
};

static const struct of_device_id of_match_clk_mt8195_mfg[] = {
	{
		.compatible = "mediatek,mt8195-mfgcfg",
		.data = &mfg_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8195_mfg);

static struct platform_driver clk_mt8195_mfg_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-mfg",
		.of_match_table = of_match_clk_mt8195_mfg,
	},
};
module_platform_driver(clk_mt8195_mfg_drv);
MODULE_LICENSE("GPL");
