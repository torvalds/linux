// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"
#include "reset.h"

#define GATE_PERI0(_id, _name, _parent, _shift)			\
		GATE_MTK(_id, _name, _parent, &peri0_cg_regs,	\
			 _shift, &mtk_clk_gate_ops_setclr)

#define GATE_PERI1(_id, _name, _parent, _shift)			\
		GATE_MTK(_id, _name, _parent, &peri1_cg_regs,	\
			 _shift, &mtk_clk_gate_ops_setclr)

static DEFINE_SPINLOCK(mt8173_clk_lock);

static const struct mtk_gate_regs peri0_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x0010,
	.sta_ofs = 0x0018,
};

static const struct mtk_gate_regs peri1_cg_regs = {
	.set_ofs = 0x000c,
	.clr_ofs = 0x0014,
	.sta_ofs = 0x001c,
};

static const char * const uart_ck_sel_parents[] = {
	"clk26m",
	"uart_sel",
};

static const struct mtk_composite peri_clks[] = {
	MUX(CLK_PERI_UART0_SEL, "uart0_ck_sel", uart_ck_sel_parents, 0x40c, 0, 1),
	MUX(CLK_PERI_UART1_SEL, "uart1_ck_sel", uart_ck_sel_parents, 0x40c, 1, 1),
	MUX(CLK_PERI_UART2_SEL, "uart2_ck_sel", uart_ck_sel_parents, 0x40c, 2, 1),
	MUX(CLK_PERI_UART3_SEL, "uart3_ck_sel", uart_ck_sel_parents, 0x40c, 3, 1),
};

static const struct mtk_gate peri_gates[] = {
	GATE_DUMMY(CLK_DUMMY, "peri_gate_dummy"),
	/* PERI0 */
	GATE_PERI0(CLK_PERI_NFI, "peri_nfi", "axi_sel", 0),
	GATE_PERI0(CLK_PERI_THERM, "peri_therm", "axi_sel", 1),
	GATE_PERI0(CLK_PERI_PWM1, "peri_pwm1", "axi_sel", 2),
	GATE_PERI0(CLK_PERI_PWM2, "peri_pwm2", "axi_sel", 3),
	GATE_PERI0(CLK_PERI_PWM3, "peri_pwm3", "axi_sel", 4),
	GATE_PERI0(CLK_PERI_PWM4, "peri_pwm4", "axi_sel", 5),
	GATE_PERI0(CLK_PERI_PWM5, "peri_pwm5", "axi_sel", 6),
	GATE_PERI0(CLK_PERI_PWM6, "peri_pwm6", "axi_sel", 7),
	GATE_PERI0(CLK_PERI_PWM7, "peri_pwm7", "axi_sel", 8),
	GATE_PERI0(CLK_PERI_PWM, "peri_pwm", "axi_sel", 9),
	GATE_PERI0(CLK_PERI_USB0, "peri_usb0", "usb20_sel", 10),
	GATE_PERI0(CLK_PERI_USB1, "peri_usb1", "usb20_sel", 11),
	GATE_PERI0(CLK_PERI_AP_DMA, "peri_ap_dma", "axi_sel", 12),
	GATE_PERI0(CLK_PERI_MSDC30_0, "peri_msdc30_0", "msdc50_0_sel", 13),
	GATE_PERI0(CLK_PERI_MSDC30_1, "peri_msdc30_1", "msdc30_1_sel", 14),
	GATE_PERI0(CLK_PERI_MSDC30_2, "peri_msdc30_2", "msdc30_2_sel", 15),
	GATE_PERI0(CLK_PERI_MSDC30_3, "peri_msdc30_3", "msdc30_3_sel", 16),
	GATE_PERI0(CLK_PERI_NLI_ARB, "peri_nli_arb", "axi_sel", 17),
	GATE_PERI0(CLK_PERI_IRDA, "peri_irda", "irda_sel", 18),
	GATE_PERI0(CLK_PERI_UART0, "peri_uart0", "axi_sel", 19),
	GATE_PERI0(CLK_PERI_UART1, "peri_uart1", "axi_sel", 20),
	GATE_PERI0(CLK_PERI_UART2, "peri_uart2", "axi_sel", 21),
	GATE_PERI0(CLK_PERI_UART3, "peri_uart3", "axi_sel", 22),
	GATE_PERI0(CLK_PERI_I2C0, "peri_i2c0", "axi_sel", 23),
	GATE_PERI0(CLK_PERI_I2C1, "peri_i2c1", "axi_sel", 24),
	GATE_PERI0(CLK_PERI_I2C2, "peri_i2c2", "axi_sel", 25),
	GATE_PERI0(CLK_PERI_I2C3, "peri_i2c3", "axi_sel", 26),
	GATE_PERI0(CLK_PERI_I2C4, "peri_i2c4", "axi_sel", 27),
	GATE_PERI0(CLK_PERI_AUXADC, "peri_auxadc", "clk26m", 28),
	GATE_PERI0(CLK_PERI_SPI0, "peri_spi0", "spi_sel", 29),
	GATE_PERI0(CLK_PERI_I2C5, "peri_i2c5", "axi_sel", 30),
	GATE_PERI0(CLK_PERI_NFIECC, "peri_nfiecc", "axi_sel", 31),
	/* PERI1 */
	GATE_PERI1(CLK_PERI_SPI, "peri_spi", "spi_sel", 0),
	GATE_PERI1(CLK_PERI_IRRX, "peri_irrx", "spi_sel", 1),
	GATE_PERI1(CLK_PERI_I2C6, "peri_i2c6", "axi_sel", 2),
};

static u16 pericfg_rst_ofs[] = { 0x0, 0x4 };

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = pericfg_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(pericfg_rst_ofs),
};

static const struct mtk_clk_desc peri_desc = {
	.clks = peri_gates,
	.num_clks = ARRAY_SIZE(peri_gates),
	.composite_clks = peri_clks,
	.num_composite_clks = ARRAY_SIZE(peri_clks),
	.clk_lock = &mt8173_clk_lock,
	.rst_desc = &clk_rst_desc,
};

static const struct of_device_id of_match_clk_mt8173_pericfg[] = {
	{ .compatible = "mediatek,mt8173-pericfg", .data = &peri_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8173_pericfg);

static struct platform_driver clk_mt8173_pericfg_drv = {
	.driver = {
		.name = "clk-mt8173-pericfg",
		.of_match_table = of_match_clk_mt8173_pericfg,
	},
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt8173_pericfg_drv);

MODULE_DESCRIPTION("MediaTek MT8173 pericfg clocks driver");
MODULE_LICENSE("GPL");
