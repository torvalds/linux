// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8192-clk.h>

static const struct mtk_gate_regs ipe_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IPE(_id, _name, _parent, _shift)	\
	GATE_MTK(_id, _name, _parent, &ipe_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate ipe_clks[] = {
	GATE_IPE(CLK_IPE_LARB19, "ipe_larb19", "ipe_sel", 0),
	GATE_IPE(CLK_IPE_LARB20, "ipe_larb20", "ipe_sel", 1),
	GATE_IPE(CLK_IPE_SMI_SUBCOM, "ipe_smi_subcom", "ipe_sel", 2),
	GATE_IPE(CLK_IPE_FD, "ipe_fd", "ipe_sel", 3),
	GATE_IPE(CLK_IPE_FE, "ipe_fe", "ipe_sel", 4),
	GATE_IPE(CLK_IPE_RSC, "ipe_rsc", "ipe_sel", 5),
	GATE_IPE(CLK_IPE_DPE, "ipe_dpe", "ipe_sel", 6),
	GATE_IPE(CLK_IPE_GALS, "ipe_gals", "ipe_sel", 8),
};

static const struct mtk_clk_desc ipe_desc = {
	.clks = ipe_clks,
	.num_clks = ARRAY_SIZE(ipe_clks),
};

static const struct of_device_id of_match_clk_mt8192_ipe[] = {
	{
		.compatible = "mediatek,mt8192-ipesys",
		.data = &ipe_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8192_ipe);

static struct platform_driver clk_mt8192_ipe_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8192-ipe",
		.of_match_table = of_match_clk_mt8192_ipe,
	},
};
module_platform_driver(clk_mt8192_ipe_drv);

MODULE_DESCRIPTION("MediaTek MT8192 Image Processing Engine clocks driver");
MODULE_LICENSE("GPL");
