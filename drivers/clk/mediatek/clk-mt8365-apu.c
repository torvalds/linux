// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs apu_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x8,
	.sta_ofs = 0x0,
};

#define GATE_APU(_id, _name, _parent, _shift) \
		GATE_MTK(_id, _name, _parent, &apu_cg_regs, _shift, \
			 &mtk_clk_gate_ops_setclr)

static const struct mtk_gate apu_clks[] = {
	GATE_APU(CLK_APU_AHB, "apu_ahb", "ifr_apu_axi", 5),
	GATE_APU(CLK_APU_EDMA, "apu_edma", "apu_sel", 4),
	GATE_APU(CLK_APU_IF_CK, "apu_if_ck", "apu_if_sel", 3),
	GATE_APU(CLK_APU_JTAG, "apu_jtag", "clk26m", 2),
	GATE_APU(CLK_APU_AXI, "apu_axi", "apu_sel", 1),
	GATE_APU(CLK_APU_IPU_CK, "apu_ck", "apu_sel", 0),
};

static const struct mtk_clk_desc apu_desc = {
	.clks = apu_clks,
	.num_clks = ARRAY_SIZE(apu_clks),
};

static const struct of_device_id of_match_clk_mt8365_apu[] = {
	{
		.compatible = "mediatek,mt8365-apu",
		.data = &apu_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8365_apu);

static struct platform_driver clk_mt8365_apu_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8365-apu",
		.of_match_table = of_match_clk_mt8365_apu,
	},
};
module_platform_driver(clk_mt8365_apu_drv);
MODULE_LICENSE("GPL");
