// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mediatek,mt6735-mfgcfg.h>

#define MFG_CG_CON			0x00
#define MFG_CG_SET			0x04
#define MFG_CG_CLR			0x08
#define MFG_RESET			0x0c

static struct mtk_gate_regs mfgcfg_cg_regs = {
	.set_ofs = MFG_CG_SET,
	.clr_ofs = MFG_CG_CLR,
	.sta_ofs = MFG_CG_CON,
};

static const struct mtk_gate mfgcfg_gates[] = {
	GATE_MTK(CLK_MFG_BG3D, "bg3d", "mfg_sel", &mfgcfg_cg_regs, 0, &mtk_clk_gate_ops_setclr),
};

static u16 mfgcfg_rst_ofs[] = { MFG_RESET };

static const struct mtk_clk_rst_desc mfgcfg_resets = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = mfgcfg_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(mfgcfg_rst_ofs)
};

static const struct mtk_clk_desc mfgcfg_clks = {
	.clks = mfgcfg_gates,
	.num_clks = ARRAY_SIZE(mfgcfg_gates),

	.rst_desc = &mfgcfg_resets
};

static const struct of_device_id of_match_mt6735_mfgcfg[] = {
	{ .compatible = "mediatek,mt6735-mfgcfg", .data = &mfgcfg_clks },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt6735_mfgcfg = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6735-mfgcfg",
		.of_match_table = of_match_mt6735_mfgcfg,
	},
};
module_platform_driver(clk_mt6735_mfgcfg);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Mediatek MT6735 mfgcfg clock and reset driver");
MODULE_LICENSE("GPL");
