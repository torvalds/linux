// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs ipe_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_IPE(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ipe_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate ipe_clks[] = {
	GATE_IPE(CLK_IPE_DPE, "ipe_dpe", "top_ipe", 0),
	GATE_IPE(CLK_IPE_FDVT, "ipe_fdvt", "top_ipe", 1),
	GATE_IPE(CLK_IPE_ME, "ipe_me", "top_ipe", 2),
	GATE_IPE(CLK_IPE_TOP, "ipe_top", "top_ipe", 3),
	GATE_IPE(CLK_IPE_SMI_LARB12, "ipe_smi_larb12", "top_ipe", 4),
};

static const struct mtk_clk_desc ipe_desc = {
	.clks = ipe_clks,
	.num_clks = ARRAY_SIZE(ipe_clks),
};

static const struct of_device_id of_match_clk_mt8195_ipe[] = {
	{
		.compatible = "mediatek,mt8195-ipesys",
		.data = &ipe_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8195_ipe_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-ipe",
		.of_match_table = of_match_clk_mt8195_ipe,
	},
};
module_platform_driver(clk_mt8195_ipe_drv);
