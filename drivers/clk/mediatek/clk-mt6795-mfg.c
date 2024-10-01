// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt6795-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs mfg_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_MFG(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &mfg_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate mfg_clks[] = {
	GATE_MFG(CLK_MFG_BAXI, "mfg_baxi", "axi_mfg_in_sel", 0),
	GATE_MFG(CLK_MFG_BMEM, "mfg_bmem", "mem_mfg_in_sel", 1),
	GATE_MFG(CLK_MFG_BG3D, "mfg_bg3d", "mfg_sel", 2),
	GATE_MFG(CLK_MFG_B26M, "mfg_b26m", "clk26m", 3),
};

static const struct mtk_clk_desc mfg_desc = {
	.clks = mfg_clks,
	.num_clks = ARRAY_SIZE(mfg_clks),
};

static const struct of_device_id of_match_clk_mt6795_mfg[] = {
	{ .compatible = "mediatek,mt6795-mfgcfg", .data = &mfg_desc },
	{ /* sentinel */ }
};

static struct platform_driver clk_mt6795_mfg_drv = {
	.driver = {
		.name = "clk-mt6795-mfg",
		.of_match_table = of_match_clk_mt6795_mfg,
	},
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt6795_mfg_drv);

MODULE_DESCRIPTION("MediaTek MT6795 mfg clocks driver");
MODULE_LICENSE("GPL");
