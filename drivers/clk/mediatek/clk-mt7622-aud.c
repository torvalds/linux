// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-gate.h"

#include <dt-bindings/clock/mt7622-clk.h>

#define GATE_AUDIO0(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_AUDIO1(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_AUDIO2(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio2_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_AUDIO3(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &audio3_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

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
	GATE_AUDIO0(CLK_AUDIO_AFE, "audio_afe", "rtc", 2),
	GATE_AUDIO0(CLK_AUDIO_HDMI, "audio_hdmi", "apll1_ck_sel", 20),
	GATE_AUDIO0(CLK_AUDIO_SPDF, "audio_spdf", "apll1_ck_sel", 21),
	GATE_AUDIO0(CLK_AUDIO_APLL, "audio_apll", "apll1_ck_sel", 23),
	/* AUDIO1 */
	GATE_AUDIO1(CLK_AUDIO_I2SIN1, "audio_i2sin1", "a1sys_hp_sel", 0),
	GATE_AUDIO1(CLK_AUDIO_I2SIN2, "audio_i2sin2", "a1sys_hp_sel", 1),
	GATE_AUDIO1(CLK_AUDIO_I2SIN3, "audio_i2sin3", "a1sys_hp_sel", 2),
	GATE_AUDIO1(CLK_AUDIO_I2SIN4, "audio_i2sin4", "a1sys_hp_sel", 3),
	GATE_AUDIO1(CLK_AUDIO_I2SO1, "audio_i2so1", "a1sys_hp_sel", 6),
	GATE_AUDIO1(CLK_AUDIO_I2SO2, "audio_i2so2", "a1sys_hp_sel", 7),
	GATE_AUDIO1(CLK_AUDIO_I2SO3, "audio_i2so3", "a1sys_hp_sel", 8),
	GATE_AUDIO1(CLK_AUDIO_I2SO4, "audio_i2so4", "a1sys_hp_sel", 9),
	GATE_AUDIO1(CLK_AUDIO_ASRCI1, "audio_asrci1", "asm_h_sel", 12),
	GATE_AUDIO1(CLK_AUDIO_ASRCI2, "audio_asrci2", "asm_h_sel", 13),
	GATE_AUDIO1(CLK_AUDIO_ASRCO1, "audio_asrco1", "asm_h_sel", 14),
	GATE_AUDIO1(CLK_AUDIO_ASRCO2, "audio_asrco2", "asm_h_sel", 15),
	GATE_AUDIO1(CLK_AUDIO_INTDIR, "audio_intdir", "intdir_sel", 20),
	GATE_AUDIO1(CLK_AUDIO_A1SYS, "audio_a1sys", "a1sys_hp_sel", 21),
	GATE_AUDIO1(CLK_AUDIO_A2SYS, "audio_a2sys", "a2sys_hp_sel", 22),
	GATE_AUDIO1(CLK_AUDIO_AFE_CONN, "audio_afe_conn", "a1sys_hp_sel", 23),
	/* AUDIO2 */
	GATE_AUDIO2(CLK_AUDIO_UL1, "audio_ul1", "a1sys_hp_sel", 0),
	GATE_AUDIO2(CLK_AUDIO_UL2, "audio_ul2", "a1sys_hp_sel", 1),
	GATE_AUDIO2(CLK_AUDIO_UL3, "audio_ul3", "a1sys_hp_sel", 2),
	GATE_AUDIO2(CLK_AUDIO_UL4, "audio_ul4", "a1sys_hp_sel", 3),
	GATE_AUDIO2(CLK_AUDIO_UL5, "audio_ul5", "a1sys_hp_sel", 4),
	GATE_AUDIO2(CLK_AUDIO_UL6, "audio_ul6", "a1sys_hp_sel", 5),
	GATE_AUDIO2(CLK_AUDIO_DL1, "audio_dl1", "a1sys_hp_sel", 6),
	GATE_AUDIO2(CLK_AUDIO_DL2, "audio_dl2", "a1sys_hp_sel", 7),
	GATE_AUDIO2(CLK_AUDIO_DL3, "audio_dl3", "a1sys_hp_sel", 8),
	GATE_AUDIO2(CLK_AUDIO_DL4, "audio_dl4", "a1sys_hp_sel", 9),
	GATE_AUDIO2(CLK_AUDIO_DL5, "audio_dl5", "a1sys_hp_sel", 10),
	GATE_AUDIO2(CLK_AUDIO_DL6, "audio_dl6", "a1sys_hp_sel", 11),
	GATE_AUDIO2(CLK_AUDIO_DLMCH, "audio_dlmch", "a1sys_hp_sel", 12),
	GATE_AUDIO2(CLK_AUDIO_ARB1, "audio_arb1", "a1sys_hp_sel", 13),
	GATE_AUDIO2(CLK_AUDIO_AWB, "audio_awb", "a1sys_hp_sel", 14),
	GATE_AUDIO2(CLK_AUDIO_AWB2, "audio_awb2", "a1sys_hp_sel", 15),
	GATE_AUDIO2(CLK_AUDIO_DAI, "audio_dai", "a1sys_hp_sel", 16),
	GATE_AUDIO2(CLK_AUDIO_MOD, "audio_mod", "a1sys_hp_sel", 17),
	/* AUDIO3 */
	GATE_AUDIO3(CLK_AUDIO_ASRCI3, "audio_asrci3", "asm_h_sel", 2),
	GATE_AUDIO3(CLK_AUDIO_ASRCI4, "audio_asrci4", "asm_h_sel", 3),
	GATE_AUDIO3(CLK_AUDIO_ASRCO3, "audio_asrco3", "asm_h_sel", 6),
	GATE_AUDIO3(CLK_AUDIO_ASRCO4, "audio_asrco4", "asm_h_sel", 7),
	GATE_AUDIO3(CLK_AUDIO_MEM_ASRC1, "audio_mem_asrc1", "asm_h_sel", 10),
	GATE_AUDIO3(CLK_AUDIO_MEM_ASRC2, "audio_mem_asrc2", "asm_h_sel", 11),
	GATE_AUDIO3(CLK_AUDIO_MEM_ASRC3, "audio_mem_asrc3", "asm_h_sel", 12),
	GATE_AUDIO3(CLK_AUDIO_MEM_ASRC4, "audio_mem_asrc4", "asm_h_sel", 13),
	GATE_AUDIO3(CLK_AUDIO_MEM_ASRC5, "audio_mem_asrc5", "asm_h_sel", 14),
};

static int clk_mt7622_audiosys_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_AUDIO_NR_CLK);

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

static const struct of_device_id of_match_clk_mt7622_aud[] = {
	{
		.compatible = "mediatek,mt7622-audsys",
		.data = clk_mt7622_audiosys_init,
	}, {
		/* sentinel */
	}
};

static int clk_mt7622_aud_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *);
	int r;

	clk_init = of_device_get_match_data(&pdev->dev);
	if (!clk_init)
		return -EINVAL;

	r = clk_init(pdev);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt7622_aud_drv = {
	.probe = clk_mt7622_aud_probe,
	.driver = {
		.name = "clk-mt7622-aud",
		.of_match_table = of_match_clk_mt7622_aud,
	},
};

builtin_platform_driver(clk_mt7622_aud_drv);
