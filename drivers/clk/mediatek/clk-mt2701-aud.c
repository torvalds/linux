// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt2701-clk.h>

#define GATE_AUDIO0(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &audio0_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

#define GATE_AUDIO1(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &audio1_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

#define GATE_AUDIO2(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &audio2_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

#define GATE_AUDIO3(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &audio3_cg_regs,			\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_no_setclr,	\
	}

static const struct mtk_gate_regs audio0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs audio1_cg_regs = {
	.set_ofs = 0x10,
	.clr_ofs = 0x10,
	.sta_ofs = 0x10,
};

static const struct mtk_gate_regs audio2_cg_regs = {
	.set_ofs = 0x14,
	.clr_ofs = 0x14,
	.sta_ofs = 0x14,
};

static const struct mtk_gate_regs audio3_cg_regs = {
	.set_ofs = 0x634,
	.clr_ofs = 0x634,
	.sta_ofs = 0x634,
};

static const struct mtk_gate audio_clks[] = {
	/* AUDIO0 */
	GATE_AUDIO0(CLK_AUD_AFE, "audio_afe", "aud_intbus_sel", 2),
	GATE_AUDIO0(CLK_AUD_HDMI, "audio_hdmi", "audpll_sel", 20),
	GATE_AUDIO0(CLK_AUD_SPDF, "audio_spdf", "audpll_sel", 21),
	GATE_AUDIO0(CLK_AUD_SPDF2, "audio_spdf2", "audpll_sel", 22),
	GATE_AUDIO0(CLK_AUD_APLL, "audio_apll", "audpll_sel", 23),
	/* AUDIO1 */
	GATE_AUDIO1(CLK_AUD_I2SIN1, "audio_i2sin1", "aud_mux1_sel", 0),
	GATE_AUDIO1(CLK_AUD_I2SIN2, "audio_i2sin2", "aud_mux1_sel", 1),
	GATE_AUDIO1(CLK_AUD_I2SIN3, "audio_i2sin3", "aud_mux1_sel", 2),
	GATE_AUDIO1(CLK_AUD_I2SIN4, "audio_i2sin4", "aud_mux1_sel", 3),
	GATE_AUDIO1(CLK_AUD_I2SIN5, "audio_i2sin5", "aud_mux1_sel", 4),
	GATE_AUDIO1(CLK_AUD_I2SIN6, "audio_i2sin6", "aud_mux1_sel", 5),
	GATE_AUDIO1(CLK_AUD_I2SO1, "audio_i2so1", "aud_mux1_sel", 6),
	GATE_AUDIO1(CLK_AUD_I2SO2, "audio_i2so2", "aud_mux1_sel", 7),
	GATE_AUDIO1(CLK_AUD_I2SO3, "audio_i2so3", "aud_mux1_sel", 8),
	GATE_AUDIO1(CLK_AUD_I2SO4, "audio_i2so4", "aud_mux1_sel", 9),
	GATE_AUDIO1(CLK_AUD_I2SO5, "audio_i2so5", "aud_mux1_sel", 10),
	GATE_AUDIO1(CLK_AUD_I2SO6, "audio_i2so6", "aud_mux1_sel", 11),
	GATE_AUDIO1(CLK_AUD_ASRCI1, "audio_asrci1", "asm_h_sel", 12),
	GATE_AUDIO1(CLK_AUD_ASRCI2, "audio_asrci2", "asm_h_sel", 13),
	GATE_AUDIO1(CLK_AUD_ASRCO1, "audio_asrco1", "asm_h_sel", 14),
	GATE_AUDIO1(CLK_AUD_ASRCO2, "audio_asrco2", "asm_h_sel", 15),
	GATE_AUDIO1(CLK_AUD_INTDIR, "audio_intdir", "intdir_sel", 20),
	GATE_AUDIO1(CLK_AUD_A1SYS, "audio_a1sys", "aud_mux1_sel", 21),
	GATE_AUDIO1(CLK_AUD_A2SYS, "audio_a2sys", "aud_mux2_sel", 22),
	GATE_AUDIO1(CLK_AUD_AFE_CONN, "audio_afe_conn", "aud_mux1_sel", 23),
	GATE_AUDIO1(CLK_AUD_AFE_MRGIF, "audio_afe_mrgif", "aud_mux1_sel", 25),
	/* AUDIO2 */
	GATE_AUDIO2(CLK_AUD_MMIF_UL1, "audio_ul1", "aud_mux1_sel", 0),
	GATE_AUDIO2(CLK_AUD_MMIF_UL2, "audio_ul2", "aud_mux1_sel", 1),
	GATE_AUDIO2(CLK_AUD_MMIF_UL3, "audio_ul3", "aud_mux1_sel", 2),
	GATE_AUDIO2(CLK_AUD_MMIF_UL4, "audio_ul4", "aud_mux1_sel", 3),
	GATE_AUDIO2(CLK_AUD_MMIF_UL5, "audio_ul5", "aud_mux1_sel", 4),
	GATE_AUDIO2(CLK_AUD_MMIF_UL6, "audio_ul6", "aud_mux1_sel", 5),
	GATE_AUDIO2(CLK_AUD_MMIF_DL1, "audio_dl1", "aud_mux1_sel", 6),
	GATE_AUDIO2(CLK_AUD_MMIF_DL2, "audio_dl2", "aud_mux1_sel", 7),
	GATE_AUDIO2(CLK_AUD_MMIF_DL3, "audio_dl3", "aud_mux1_sel", 8),
	GATE_AUDIO2(CLK_AUD_MMIF_DL4, "audio_dl4", "aud_mux1_sel", 9),
	GATE_AUDIO2(CLK_AUD_MMIF_DL5, "audio_dl5", "aud_mux1_sel", 10),
	GATE_AUDIO2(CLK_AUD_MMIF_DL6, "audio_dl6", "aud_mux1_sel", 11),
	GATE_AUDIO2(CLK_AUD_MMIF_DLMCH, "audio_dlmch", "aud_mux1_sel", 12),
	GATE_AUDIO2(CLK_AUD_MMIF_ARB1, "audio_arb1", "aud_mux1_sel", 13),
	GATE_AUDIO2(CLK_AUD_MMIF_AWB1, "audio_awb", "aud_mux1_sel", 14),
	GATE_AUDIO2(CLK_AUD_MMIF_AWB2, "audio_awb2", "aud_mux1_sel", 15),
	GATE_AUDIO2(CLK_AUD_MMIF_DAI, "audio_dai", "aud_mux1_sel", 16),
	/* AUDIO3 */
	GATE_AUDIO3(CLK_AUD_ASRCI3, "audio_asrci3", "asm_h_sel", 2),
	GATE_AUDIO3(CLK_AUD_ASRCI4, "audio_asrci4", "asm_h_sel", 3),
	GATE_AUDIO3(CLK_AUD_ASRCI5, "audio_asrci5", "asm_h_sel", 4),
	GATE_AUDIO3(CLK_AUD_ASRCI6, "audio_asrci6", "asm_h_sel", 5),
	GATE_AUDIO3(CLK_AUD_ASRCO3, "audio_asrco3", "asm_h_sel", 6),
	GATE_AUDIO3(CLK_AUD_ASRCO4, "audio_asrco4", "asm_h_sel", 7),
	GATE_AUDIO3(CLK_AUD_ASRCO5, "audio_asrco5", "asm_h_sel", 8),
	GATE_AUDIO3(CLK_AUD_ASRCO6, "audio_asrco6", "asm_h_sel", 9),
	GATE_AUDIO3(CLK_AUD_MEM_ASRC1, "audio_mem_asrc1", "asm_h_sel", 10),
	GATE_AUDIO3(CLK_AUD_MEM_ASRC2, "audio_mem_asrc2", "asm_h_sel", 11),
	GATE_AUDIO3(CLK_AUD_MEM_ASRC3, "audio_mem_asrc3", "asm_h_sel", 12),
	GATE_AUDIO3(CLK_AUD_MEM_ASRC4, "audio_mem_asrc4", "asm_h_sel", 13),
	GATE_AUDIO3(CLK_AUD_MEM_ASRC5, "audio_mem_asrc5", "asm_h_sel", 14),
};

static const struct of_device_id of_match_clk_mt2701_aud[] = {
	{ .compatible = "mediatek,mt2701-audsys", },
	{}
};

static int clk_mt2701_aud_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_AUD_NR);

	mtk_clk_register_gates(node, audio_clks, ARRAY_SIZE(audio_clks),
			       clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r) {
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

		goto err_clk_provider;
	}

	r = devm_of_platform_populate(&pdev->dev);
	if (r)
		goto err_plat_populate;

	return 0;

err_plat_populate:
	of_clk_del_provider(node);
err_clk_provider:
	return r;
}

static struct platform_driver clk_mt2701_aud_drv = {
	.probe = clk_mt2701_aud_probe,
	.driver = {
		.name = "clk-mt2701-aud",
		.of_match_table = of_match_clk_mt2701_aud,
	},
};

builtin_platform_driver(clk_mt2701_aud_drv);
