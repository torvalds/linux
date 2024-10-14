// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Copyright (c) 2020 BayLibre, SAS
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *         Fabien Parent <fparent@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8167-clk.h>

static const struct mtk_gate_regs aud_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_AUD(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &aud_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)


static const struct mtk_gate aud_clks[] = {
	GATE_AUD(CLK_AUD_AFE, "aud_afe", "clk26m_ck", 2),
	GATE_AUD(CLK_AUD_I2S, "aud_i2s", "i2s_infra_bck", 6),
	GATE_AUD(CLK_AUD_22M, "aud_22m", "rg_aud_engen1", 8),
	GATE_AUD(CLK_AUD_24M, "aud_24m", "rg_aud_engen2", 9),
	GATE_AUD(CLK_AUD_INTDIR, "aud_intdir", "rg_aud_spdif_in", 15),
	GATE_AUD(CLK_AUD_APLL2_TUNER, "aud_apll2_tuner", "rg_aud_engen2", 18),
	GATE_AUD(CLK_AUD_APLL_TUNER, "aud_apll_tuner", "rg_aud_engen1", 19),
	GATE_AUD(CLK_AUD_HDMI, "aud_hdmi", "apll12_div4", 20),
	GATE_AUD(CLK_AUD_SPDF, "aud_spdf", "apll12_div6", 21),
	GATE_AUD(CLK_AUD_ADC, "aud_adc", "aud_afe", 24),
	GATE_AUD(CLK_AUD_DAC, "aud_dac", "aud_afe", 25),
	GATE_AUD(CLK_AUD_DAC_PREDIS, "aud_dac_predis", "aud_afe", 26),
	GATE_AUD(CLK_AUD_TML, "aud_tml", "aud_afe", 27),
};

static const struct mtk_clk_desc aud_desc = {
	.clks = aud_clks,
	.num_clks = ARRAY_SIZE(aud_clks),
};

static const struct of_device_id of_match_clk_mt8167_audsys[] = {
	{ .compatible = "mediatek,mt8167-audsys", .data = &aud_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8167_audsys);

static struct platform_driver clk_mt8167_audsys_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8167-audsys",
		.of_match_table = of_match_clk_mt8167_audsys,
	},
};
module_platform_driver(clk_mt8167_audsys_drv);

MODULE_DESCRIPTION("MediaTek MT8167 audio clocks driver");
MODULE_LICENSE("GPL");
