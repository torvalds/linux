// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs peri_ao0_cg_regs = {
	.set_ofs = 0x24,
	.clr_ofs = 0x28,
	.sta_ofs = 0x10,
};

static const struct mtk_gate_regs peri_ao1_cg_regs = {
	.set_ofs = 0x2c,
	.clr_ofs = 0x30,
	.sta_ofs = 0x14,
};

static const struct mtk_gate_regs peri_ao1_hwv_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x000c,
	.sta_ofs = 0x2c04,
};

static const struct mtk_gate_regs peri_ao2_cg_regs = {
	.set_ofs = 0x34,
	.clr_ofs = 0x38,
	.sta_ofs = 0x18,
};

#define GATE_PERI_AO0(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &peri_ao0_cg_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

#define GATE_PERI_AO1(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &peri_ao1_cg_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

#define GATE_HWV_PERI_AO1(_id, _name, _parent, _shift) {\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &peri_ao1_cg_regs,		\
		.hwv_regs = &peri_ao1_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
	}

#define GATE_PERI_AO2(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &peri_ao2_cg_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_ops_setclr,	\
	}

static const struct mtk_gate peri_ao_clks[] = {
	/* PERI_AO0 */
	GATE_PERI_AO0(CLK_PERI_AO_UART0_BCLK, "peri_ao_uart0_bclk", "uart", 0),
	GATE_PERI_AO0(CLK_PERI_AO_UART1_BCLK, "peri_ao_uart1_bclk", "uart", 1),
	GATE_PERI_AO0(CLK_PERI_AO_UART2_BCLK, "peri_ao_uart2_bclk", "uart", 2),
	GATE_PERI_AO0(CLK_PERI_AO_UART3_BCLK, "peri_ao_uart3_bclk", "uart", 3),
	GATE_PERI_AO0(CLK_PERI_AO_UART4_BCLK, "peri_ao_uart4_bclk", "uart", 4),
	GATE_PERI_AO0(CLK_PERI_AO_UART5_BCLK, "peri_ao_uart5_bclk", "uart", 5),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_X16W_HCLK, "peri_ao_pwm_x16w", "p_axi", 12),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_X16W_BCLK, "peri_ao_pwm_x16w_bclk", "pwm", 13),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_PWM_BCLK0, "peri_ao_pwm_pwm_bclk0", "pwm", 14),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_PWM_BCLK1, "peri_ao_pwm_pwm_bclk1", "pwm", 15),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_PWM_BCLK2, "peri_ao_pwm_pwm_bclk2", "pwm", 16),
	GATE_PERI_AO0(CLK_PERI_AO_PWM_PWM_BCLK3, "peri_ao_pwm_pwm_bclk3", "pwm", 17),
	/* PERI_AO1 */
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI0_BCLK, "peri_ao_spi0_bclk", "spi0_b", 0),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI1_BCLK, "peri_ao_spi1_bclk", "spi1_b", 2),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI2_BCLK, "peri_ao_spi2_bclk", "spi2_b", 3),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI3_BCLK, "peri_ao_spi3_bclk", "spi3_b", 4),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI4_BCLK, "peri_ao_spi4_bclk", "spi4_b", 5),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI5_BCLK, "peri_ao_spi5_bclk", "spi5_b", 6),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI6_BCLK, "peri_ao_spi6_bclk", "spi6_b", 7),
	GATE_HWV_PERI_AO1(CLK_PERI_AO_SPI7_BCLK, "peri_ao_spi7_bclk", "spi7_b", 8),
	GATE_PERI_AO1(CLK_PERI_AO_FLASHIF_FLASH, "peri_ao_flashif_flash", "peri_ao_flashif_27m",
		      18),
	GATE_PERI_AO1(CLK_PERI_AO_FLASHIF_27M, "peri_ao_flashif_27m", "sflash", 19),
	GATE_PERI_AO1(CLK_PERI_AO_FLASHIF_DRAM, "peri_ao_flashif_dram", "p_axi", 20),
	GATE_PERI_AO1(CLK_PERI_AO_FLASHIF_AXI, "peri_ao_flashif_axi", "peri_ao_flashif_dram", 21),
	GATE_PERI_AO1(CLK_PERI_AO_FLASHIF_BCLK, "peri_ao_flashif_bclk", "p_axi", 22),
	GATE_PERI_AO1(CLK_PERI_AO_AP_DMA_X32W_BCLK, "peri_ao_ap_dma_x32w_bclk", "p_axi", 26),
	/* PERI_AO2 */
	GATE_PERI_AO2(CLK_PERI_AO_MSDC1_MSDC_SRC, "peri_ao_msdc1_msdc_src", "msdc30_1", 1),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC1_HCLK, "peri_ao_msdc1", "peri_ao_msdc1_axi", 2),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC1_AXI, "peri_ao_msdc1_axi", "p_axi", 3),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC1_HCLK_WRAP, "peri_ao_msdc1_h_wrap", "peri_ao_msdc1", 4),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC2_MSDC_SRC, "peri_ao_msdc2_msdc_src", "msdc30_2", 10),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC2_HCLK, "peri_ao_msdc2", "peri_ao_msdc2_axi", 11),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC2_AXI, "peri_ao_msdc2_axi", "p_axi", 12),
	GATE_PERI_AO2(CLK_PERI_AO_MSDC2_HCLK_WRAP, "peri_ao_msdc2_h_wrap", "peri_ao_msdc2", 13),
};

static const struct mtk_clk_desc peri_ao_mcd = {
	.clks = peri_ao_clks,
	.num_clks = ARRAY_SIZE(peri_ao_clks),
};

static const struct of_device_id of_match_clk_mt8196_peri_ao[] = {
	{ .compatible = "mediatek,mt8196-pericfg-ao", .data = &peri_ao_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8196_peri_ao);

static struct platform_driver clk_mt8196_peri_ao_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8196-peri-ao",
		.of_match_table = of_match_clk_mt8196_peri_ao,
	},
};

MODULE_DESCRIPTION("MediaTek MT8196 pericfg_ao clock controller driver");
module_platform_driver(clk_mt8196_peri_ao_drv);
MODULE_LICENSE("GPL");
