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

static const struct mtk_clk_desc ipu_adl_desc = {
	.clks = ipu_adl_clks,
	.num_clks = ARRAY_SIZE(ipu_adl_clks),
};

static const struct of_device_id of_match_clk_mt8183_ipu_adl[] = {
	{
		.compatible = "mediatek,mt8183-ipu_adl",
		.data = &ipu_adl_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8183_ipu_adl);

static struct platform_driver clk_mt8183_ipu_adl_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8183-ipu_adl",
		.of_match_table = of_match_clk_mt8183_ipu_adl,
	},
};
module_platform_driver(clk_mt8183_ipu_adl_drv);
MODULE_LICENSE("GPL");
