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

static const struct mtk_gate_regs mfg_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_MFG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mfg_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate mfg_clks[] = {
	GATE_MFG(CLK_MFGCFG_BG3D, "mfg_bg3d", "mfg_sel", 0),
};

static const struct mtk_clk_desc mfg_desc = {
	.clks = mfg_clks,
	.num_clks = ARRAY_SIZE(mfg_clks),
};

static const struct of_device_id of_match_clk_mt6779_mfg[] = {
	{
		.compatible = "mediatek,mt6779-mfgcfg",
		.data = &mfg_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6779_mfg);

static struct platform_driver clk_mt6779_mfg_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6779-mfg",
		.of_match_table = of_match_clk_mt6779_mfg,
	},
};

module_platform_driver(clk_mt6779_mfg_drv);
MODULE_LICENSE("GPL");
