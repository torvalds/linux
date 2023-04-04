// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Garmin Chang <garmin.chang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/mediatek,mt8188-clk.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs adsp_audio26m_cg_regs = {
	.set_ofs = 0x80,
	.clr_ofs = 0x80,
	.sta_ofs = 0x80,
};

#define GATE_ADSP_FLAGS(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &adsp_audio26m_cg_regs, _shift,		\
		&mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate adsp_audio26m_clks[] = {
	GATE_ADSP_FLAGS(CLK_AUDIODSP_AUDIO26M, "audiodsp_audio26m", "clk26m", 3),
};

static const struct mtk_clk_desc adsp_audio26m_desc = {
	.clks = adsp_audio26m_clks,
	.num_clks = ARRAY_SIZE(adsp_audio26m_clks),
};

static const struct of_device_id of_match_clk_mt8188_adsp_audio26m[] = {
	{ .compatible = "mediatek,mt8188-adsp-audio26m", .data = &adsp_audio26m_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8188_adsp_audio26m);

static struct platform_driver clk_mt8188_adsp_audio26m_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8188-adsp_audio26m",
		.of_match_table = of_match_clk_mt8188_adsp_audio26m,
	},
};
module_platform_driver(clk_mt8188_adsp_audio26m_drv);
MODULE_LICENSE("GPL");
