// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Wendell Lin <wendell.lin@mediatek.com>
 */

#include <linux/module.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt6779-clk.h>

static const struct mtk_gate_regs audio0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs audio1_cg_regs = {
	.set_ofs = 0x4,
	.clr_ofs = 0x4,
	.sta_ofs = 0x4,
};

#define GATE_AUDIO0(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio0_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr)
#define GATE_AUDIO1(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio1_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate audio_clks[] = {
	/* AUDIO0 */
	GATE_AUDIO0(CLK_AUD_AFE, "aud_afe", "audio_sel", 2),
	GATE_AUDIO0(CLK_AUD_22M, "aud_22m", "aud_eng1_sel", 8),
	GATE_AUDIO0(CLK_AUD_24M, "aud_24m", "aud_eng2_sel", 9),
	GATE_AUDIO0(CLK_AUD_APLL2_TUNER, "aud_apll2_tuner",
		    "aud_eng2_sel", 18),
	GATE_AUDIO0(CLK_AUD_APLL_TUNER, "aud_apll_tuner",
		    "aud_eng1_sel", 19),
	GATE_AUDIO0(CLK_AUD_TDM, "aud_tdm", "aud_eng1_sel", 20),
	GATE_AUDIO0(CLK_AUD_ADC, "aud_adc", "audio_sel", 24),
	GATE_AUDIO0(CLK_AUD_DAC, "aud_dac", "audio_sel", 25),
	GATE_AUDIO0(CLK_AUD_DAC_PREDIS, "aud_dac_predis",
		    "audio_sel", 26),
	GATE_AUDIO0(CLK_AUD_TML, "aud_tml", "audio_sel", 27),
	GATE_AUDIO0(CLK_AUD_NLE, "aud_nle", "audio_sel", 28),
	/* AUDIO1 */
	GATE_AUDIO1(CLK_AUD_I2S1_BCLK_SW, "aud_i2s1_bclk",
		    "audio_sel", 4),
	GATE_AUDIO1(CLK_AUD_I2S2_BCLK_SW, "aud_i2s2_bclk",
		    "audio_sel", 5),
	GATE_AUDIO1(CLK_AUD_I2S3_BCLK_SW, "aud_i2s3_bclk",
		    "audio_sel", 6),
	GATE_AUDIO1(CLK_AUD_I2S4_BCLK_SW, "aud_i2s4_bclk",
		    "audio_sel", 7),
	GATE_AUDIO1(CLK_AUD_I2S5_BCLK_SW, "aud_i2s5_bclk",
		    "audio_sel", 8),
	GATE_AUDIO1(CLK_AUD_CONN_I2S_ASRC, "aud_conn_i2s",
		    "audio_sel", 12),
	GATE_AUDIO1(CLK_AUD_GENERAL1_ASRC, "aud_general1",
		    "audio_sel", 13),
	GATE_AUDIO1(CLK_AUD_GENERAL2_ASRC, "aud_general2",
		    "audio_sel", 14),
	GATE_AUDIO1(CLK_AUD_DAC_HIRES, "aud_dac_hires",
		    "audio_h_sel", 15),
	GATE_AUDIO1(CLK_AUD_ADC_HIRES, "aud_adc_hires",
		    "audio_h_sel", 16),
	GATE_AUDIO1(CLK_AUD_ADC_HIRES_TML, "aud_adc_hires_tml",
		    "audio_h_sel", 17),
	GATE_AUDIO1(CLK_AUD_PDN_ADDA6_ADC, "aud_pdn_adda6_adc",
		    "audio_sel", 20),
	GATE_AUDIO1(CLK_AUD_ADDA6_ADC_HIRES, "aud_adda6_adc_hires",
		    "audio_h_sel",
		    21),
	GATE_AUDIO1(CLK_AUD_3RD_DAC, "aud_3rd_dac", "audio_sel",
		    28),
	GATE_AUDIO1(CLK_AUD_3RD_DAC_PREDIS, "aud_3rd_dac_predis",
		    "audio_sel", 29),
	GATE_AUDIO1(CLK_AUD_3RD_DAC_TML, "aud_3rd_dac_tml",
		    "audio_sel", 30),
	GATE_AUDIO1(CLK_AUD_3RD_DAC_HIRES, "aud_3rd_dac_hires",
		    "audio_h_sel", 31),
};

static const struct mtk_clk_desc audio_desc = {
	.clks = audio_clks,
	.num_clks = ARRAY_SIZE(audio_clks),
};

static const struct of_device_id of_match_clk_mt6779_aud[] = {
	{
		.compatible = "mediatek,mt6779-audio",
		.data = &audio_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6779_aud);

static struct platform_driver clk_mt6779_aud_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6779-aud",
		.of_match_table = of_match_clk_mt6779_aud,
	},
};

module_platform_driver(clk_mt6779_aud_drv);
MODULE_LICENSE("GPL");
