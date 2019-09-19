// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Wendell Lin <wendell.lin@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6779-clk.h>

static const struct mtk_gate_regs vdec0_cg_regs = {
	.set_ofs = 0x0000,
	.clr_ofs = 0x0004,
	.sta_ofs = 0x0000,
};

static const struct mtk_gate_regs vdec1_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x000c,
	.sta_ofs = 0x0008,
};

#define GATE_VDEC0_I(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &vdec0_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr_inv)
#define GATE_VDEC1_I(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &vdec1_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr_inv)

static const struct mtk_gate vdec_clks[] = {
	/* VDEC0 */
	GATE_VDEC0_I(CLK_VDEC_VDEC, "vdec_cken", "vdec_sel", 0),
	/* VDEC1 */
	GATE_VDEC1_I(CLK_VDEC_LARB1, "vdec_larb1_cken", "vdec_sel", 0),
};

static const struct of_device_id of_match_clk_mt6779_vdec[] = {
	{ .compatible = "mediatek,mt6779-vdecsys", },
	{}
};

static int clk_mt6779_vdec_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_VDEC_GCON_NR_CLK);

	mtk_clk_register_gates(node, vdec_clks, ARRAY_SIZE(vdec_clks),
			       clk_data);

	return of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

static struct platform_driver clk_mt6779_vdec_drv = {
	.probe = clk_mt6779_vdec_probe,
	.driver = {
		.name = "clk-mt6779-vdec",
		.of_match_table = of_match_clk_mt6779_vdec,
	},
};

builtin_platform_driver(clk_mt6779_vdec_drv);
