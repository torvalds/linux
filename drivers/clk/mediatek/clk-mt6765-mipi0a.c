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

#define GATE_MIPI0A(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mipi0a_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate mipi0a_clks[] = {
	GATE_MIPI0A(CLK_MIPI0A_CSR_CSI_EN_0A,
		    "mipi0a_csr_0a", "f_fseninf_ck", 1),
};

static const struct mtk_clk_desc mipi0a_desc = {
	.clks = mipi0a_clks,
	.num_clks = ARRAY_SIZE(mipi0a_clks),
};

static const struct of_device_id of_match_clk_mt6765_mipi0a[] = {
	{
		.compatible = "mediatek,mt6765-mipi0a",
		.data = &mipi0a_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt6765_mipi0a_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6765-mipi0a",
		.of_match_table = of_match_clk_mt6765_mipi0a,
	},
};
module_platform_driver(clk_mt6765_mipi0a_drv);
MODULE_LICENSE("GPL");
