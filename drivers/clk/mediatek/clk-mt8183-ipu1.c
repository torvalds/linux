// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8183-clk.h>

static const struct mtk_gate_regs ipu_core1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_IPU_CORE1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ipu_core1_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate ipu_core1_clks[] = {
	GATE_IPU_CORE1(CLK_IPU_CORE1_JTAG, "ipu_core1_jtag", "dsp_sel", 0),
	GATE_IPU_CORE1(CLK_IPU_CORE1_AXI, "ipu_core1_axi", "dsp_sel", 1),
	GATE_IPU_CORE1(CLK_IPU_CORE1_IPU, "ipu_core1_ipu", "dsp_sel", 2),
};

static int clk_mt8183_ipu_core1_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_IPU_CORE1_NR_CLK);

	mtk_clk_register_gates(node, ipu_core1_clks, ARRAY_SIZE(ipu_core1_clks),
			clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static const struct of_device_id of_match_clk_mt8183_ipu_core1[] = {
	{ .compatible = "mediatek,mt8183-ipu_core1", },
	{}
};

static struct platform_driver clk_mt8183_ipu_core1_drv = {
	.probe = clk_mt8183_ipu_core1_probe,
	.driver = {
		.name = "clk-mt8183-ipu_core1",
		.of_match_table = of_match_clk_mt8183_ipu_core1,
	},
};

builtin_platform_driver(clk_mt8183_ipu_core1_drv);
