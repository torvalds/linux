// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs wpe_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_WPE(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &wpe_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate wpe_clks[] = {
	GATE_WPE(CLK_WPE_CK_EN, "wpe", "top_wpe", 17),
	GATE_WPE(CLK_WPE_SMI_LARB8_CK_EN, "wpe_smi_larb8", "top_wpe", 19),
	GATE_WPE(CLK_WPE_SYS_EVENT_TX_CK_EN, "wpe_sys_event_tx", "top_wpe", 20),
	GATE_WPE(CLK_WPE_SMI_LARB8_PCLK_EN, "wpe_smi_larb8_p_en", "top_wpe", 25),
};

static const struct mtk_clk_desc wpe_desc = {
	.clks = wpe_clks,
	.num_clks = ARRAY_SIZE(wpe_clks),
};

static const struct of_device_id of_match_clk_mt8186_wpe[] = {
	{
		.compatible = "mediatek,mt8186-wpesys",
		.data = &wpe_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8186_wpe);

static struct platform_driver clk_mt8186_wpe_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8186-wpe",
		.of_match_table = of_match_clk_mt8186_wpe,
	},
};
module_platform_driver(clk_mt8186_wpe_drv);

MODULE_DESCRIPTION("MediaTek MT8186 Warp Engine clocks driver");
MODULE_LICENSE("GPL");
