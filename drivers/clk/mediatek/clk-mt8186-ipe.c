// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs ipe_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IPE(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ipe_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate ipe_clks[] = {
	GATE_IPE(CLK_IPE_LARB19, "ipe_larb19", "top_ipe", 0),
	GATE_IPE(CLK_IPE_LARB20, "ipe_larb20", "top_ipe", 1),
	GATE_IPE(CLK_IPE_SMI_SUBCOM, "ipe_smi_subcom", "top_ipe", 2),
	GATE_IPE(CLK_IPE_FD, "ipe_fd", "top_ipe", 3),
	GATE_IPE(CLK_IPE_FE, "ipe_fe", "top_ipe", 4),
	GATE_IPE(CLK_IPE_RSC, "ipe_rsc", "top_ipe", 5),
	GATE_IPE(CLK_IPE_DPE, "ipe_dpe", "top_ipe", 6),
	GATE_IPE(CLK_IPE_GALS_IPE, "ipe_gals_ipe", "top_img1", 8),
};

static const struct mtk_clk_desc ipe_desc = {
	.clks = ipe_clks,
	.num_clks = ARRAY_SIZE(ipe_clks),
};

static const struct of_device_id of_match_clk_mt8186_ipe[] = {
	{
		.compatible = "mediatek,mt8186-ipesys",
		.data = &ipe_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8186_ipe_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-ipe",
		.of_match_table = of_match_clk_mt8186_ipe,
	},
};
module_platform_driver(clk_mt8186_ipe_drv);
