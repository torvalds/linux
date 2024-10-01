// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mfg0_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs mfg1_cg_regs = {
	.set_ofs = 0x280,
	.clr_ofs = 0x280,
	.sta_ofs = 0x280,
};

#define GATE_MFG0(_id, _name, _parent, _shift) \
		GATE_MTK(_id, _name, _parent, &mfg0_cg_regs, _shift, \
			 &mtk_clk_gate_ops_setclr)

#define GATE_MFG1(_id, _name, _parent, _shift) \
		GATE_MTK(_id, _name, _parent, &mfg1_cg_regs, _shift, \
			 &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate mfg_clks[] = {
	/* MFG0 */
	GATE_MFG0(CLK_MFG_BG3D, "mfg_bg3d", "mfg_sel", 0),
	/* MFG1 */
	GATE_MFG1(CLK_MFG_MBIST_DIAG, "mfg_mbist_diag", "mbist_diag_sel", 24),
};

static const struct mtk_clk_desc mfg_desc = {
	.clks = mfg_clks,
	.num_clks = ARRAY_SIZE(mfg_clks),
};

static const struct of_device_id of_match_clk_mt8365_mfg[] = {
	{
		.compatible = "mediatek,mt8365-mfgcfg",
		.data = &mfg_desc,
	}, {
		/* sentinel */
	}
};

static struct platform_driver clk_mt8365_mfg_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8365-mfg",
		.of_match_table = of_match_clk_mt8365_mfg,
	},
};
builtin_platform_driver(clk_mt8365_mfg_drv);
MODULE_LICENSE("GPL");
