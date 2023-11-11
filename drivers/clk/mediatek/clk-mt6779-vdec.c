// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Wendell Lin <wendell.lin@mediatek.com>
 */

#include <linux/module.h>
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

static const struct mtk_clk_desc vdec_desc = {
	.clks = vdec_clks,
	.num_clks = ARRAY_SIZE(vdec_clks),
};

static const struct of_device_id of_match_clk_mt6779_vdec[] = {
	{
		.compatible = "mediatek,mt6779-vdecsys",
		.data = &vdec_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6779_vdec_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6779-vdec",
		.of_match_table = of_match_clk_mt6779_vdec,
	},
};

module_platform_driver(clk_mt6779_vdec_drv);
MODULE_LICENSE("GPL");
