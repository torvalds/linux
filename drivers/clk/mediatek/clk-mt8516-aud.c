// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 *         Fabien Parent <fparent@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt8516-clk.h>

static const struct mtk_gate_regs aud_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

#define GATE_AUD(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &aud_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate aud_clks[] __initconst = {
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

static void __init mtk_audsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_AUD_NR_CLK);

	mtk_clk_register_gates(node, aud_clks, ARRAY_SIZE(aud_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

}
CLK_OF_DECLARE(mtk_audsys, "mediatek,mt8516-audsys", mtk_audsys_init);
