// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8183-clk.h>

static const struct mtk_gate_regs ipu_adl_cg_regs = {
	.set_ofs = 0x204,
	.clr_ofs = 0x204,
	.sta_ofs = 0x204,
};

#define GATE_IPU_ADL_I(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &ipu_adl_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate ipu_adl_clks[] = {
	GATE_IPU_ADL_I(CLK_IPU_ADL_CABGEN, "ipu_adl_cabgen", "dsp_sel", 24),
};

static int clk_mt8183_ipu_adl_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_IPU_ADL_NR_CLK);

	mtk_clk_register_gates(node, ipu_adl_clks, ARRAY_SIZE(ipu_adl_clks),
			clk_data);

	return of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

static const struct of_device_id of_match_clk_mt8183_ipu_adl[] = {
	{ .compatible = "mediatek,mt8183-ipu_adl", },
	{}
};

static struct platform_driver clk_mt8183_ipu_adl_drv = {
	.probe = clk_mt8183_ipu_adl_probe,
	.driver = {
		.name = "clk-mt8183-ipu_adl",
		.of_match_table = of_match_clk_mt8183_ipu_adl,
	},
};

builtin_platform_driver(clk_mt8183_ipu_adl_drv);
