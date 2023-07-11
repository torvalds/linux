// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Wendell Lin <wendell.lin@mediatek.com>
 */

#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt6779-clk.h>

#include "clk-mtk.h"
#include "clk-gate.h"

static const struct mtk_gate_regs ipe_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_IPE(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &ipe_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate ipe_clks[] = {
	GATE_IPE(CLK_IPE_LARB7, "ipe_larb7", "ipe_sel", 0),
	GATE_IPE(CLK_IPE_LARB8, "ipe_larb8", "ipe_sel", 1),
	GATE_IPE(CLK_IPE_SMI_SUBCOM, "ipe_smi_subcom", "ipe_sel", 2),
	GATE_IPE(CLK_IPE_FD, "ipe_fd", "ipe_sel", 3),
	GATE_IPE(CLK_IPE_FE, "ipe_fe", "ipe_sel", 4),
	GATE_IPE(CLK_IPE_RSC, "ipe_rsc", "ipe_sel", 5),
	GATE_IPE(CLK_IPE_DPE, "ipe_dpe", "ipe_sel", 6),
};

static const struct mtk_clk_desc ipe_desc = {
	.clks = ipe_clks,
	.num_clks = ARRAY_SIZE(ipe_clks),
};

static const struct of_device_id of_match_clk_mt6779_ipe[] = {
	{
		.compatible = "mediatek,mt6779-ipesys",
		.data = &ipe_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6779_ipe);

static struct platform_driver clk_mt6779_ipe_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6779-ipe",
		.of_match_table = of_match_clk_mt6779_ipe,
	},
};

module_platform_driver(clk_mt6779_ipe_drv);
MODULE_LICENSE("GPL");
