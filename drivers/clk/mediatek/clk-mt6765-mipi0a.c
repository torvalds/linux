// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6765-clk.h>

static const struct mtk_gate_regs mipi0a_cg_regs = {
	.set_ofs = 0x80,
	.clr_ofs = 0x80,
	.sta_ofs = 0x80,
};

#define GATE_MIPI0A(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &mipi0a_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr_inv,	\
	}

static const struct mtk_gate mipi0a_clks[] = {
	GATE_MIPI0A(CLK_MIPI0A_CSR_CSI_EN_0A,
		    "mipi0a_csr_0a", "f_fseninf_ck", 1),
};

static int clk_mt6765_mipi0a_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_MIPI0A_NR_CLK);

	mtk_clk_register_gates(node, mipi0a_clks,
			       ARRAY_SIZE(mipi0a_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	return r;
}

static const struct of_device_id of_match_clk_mt6765_mipi0a[] = {
	{ .compatible = "mediatek,mt6765-mipi0a", },
	{}
};

static struct platform_driver clk_mt6765_mipi0a_drv = {
	.probe = clk_mt6765_mipi0a_probe,
	.driver = {
		.name = "clk-mt6765-mipi0a",
		.of_match_table = of_match_clk_mt6765_mipi0a,
	},
};

builtin_platform_driver(clk_mt6765_mipi0a_drv);
