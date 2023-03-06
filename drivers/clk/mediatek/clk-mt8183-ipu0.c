// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8183-clk.h>

static const struct mtk_gate_regs ipu_core0_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IPU_CORE0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ipu_core0_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate ipu_core0_clks[] = {
	GATE_IPU_CORE0(CLK_IPU_CORE0_JTAG, "ipu_core0_jtag", "dsp_sel", 0),
	GATE_IPU_CORE0(CLK_IPU_CORE0_AXI, "ipu_core0_axi", "dsp_sel", 1),
	GATE_IPU_CORE0(CLK_IPU_CORE0_IPU, "ipu_core0_ipu", "dsp_sel", 2),
};

static const struct mtk_clk_desc ipu_core0_desc = {
	.clks = ipu_core0_clks,
	.num_clks = ARRAY_SIZE(ipu_core0_clks),
};

static const struct of_device_id of_match_clk_mt8183_ipu_core0[] = {
	{
		.compatible = "mediatek,mt8183-ipu_core0",
		.data = &ipu_core0_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8183_ipu_core0_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8183-ipu_core0",
		.of_match_table = of_match_clk_mt8183_ipu_core0,
	},
};
module_platform_driver(clk_mt8183_ipu_core0_drv);
