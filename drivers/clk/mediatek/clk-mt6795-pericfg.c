// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt6795-clk.h>
#include <dt-bindings/reset/mediatek,mt6795-resets.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"
#include "reset.h"

#define GATE_PERI(_id, _name, _parent, _shift)			\
		GATE_MTK(_id, _name, _parent, &peri_cg_regs,	\
			 _shift, &mtk_clk_gate_ops_setclr)

static DEFINE_SPINLOCK(mt6795_peri_clk_lock);

static const struct mtk_gate_regs peri_cg_regs = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x0010,
	.sta_ofs = 0x0018,
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
	GATE_PERI(CLK_PERI_NFI, "peri_nfi", "axi_sel", 0),
	GATE_PERI(CLK_PERI_THERM, "peri_therm", "axi_sel", 1),
	GATE_PERI(CLK_PERI_PWM1, "peri_pwm1", "axi_sel", 2),
	GATE_PERI(CLK_PERI_PWM2, "peri_pwm2", "axi_sel", 3),
	GATE_PERI(CLK_PERI_PWM3, "peri_pwm3", "axi_sel", 4),
	GATE_PERI(CLK_PERI_PWM4, "peri_pwm4", "axi_sel", 5),
	GATE_PERI(CLK_PERI_PWM5, "peri_pwm5", "axi_sel", 6),
	GATE_PERI(CLK_PERI_PWM6, "peri_pwm6", "axi_sel", 7),
	GATE_PERI(CLK_PERI_PWM7, "peri_pwm7", "axi_sel", 8),
	GATE_PERI(CLK_PERI_PWM, "peri_pwm", "axi_sel", 9),
	GATE_PERI(CLK_PERI_USB0, "peri_usb0", "usb30_sel", 10),
	GATE_PERI(CLK_PERI_USB1, "peri_usb1", "usb20_sel", 11),
	GATE_PERI(CLK_PERI_AP_DMA, "peri_ap_dma", "axi_sel", 12),
	GATE_PERI(CLK_PERI_MSDC30_0, "peri_msdc30_0", "msdc50_0_sel", 13),
	GATE_PERI(CLK_PERI_MSDC30_1, "peri_msdc30_1", "msdc30_1_sel", 14),
	GATE_PERI(CLK_PERI_MSDC30_2, "peri_msdc30_2", "msdc30_2_sel", 15),
	GATE_PERI(CLK_PERI_MSDC30_3, "peri_msdc30_3", "msdc30_3_sel", 16),
	GATE_PERI(CLK_PERI_NLI_ARB, "peri_nli_arb", "axi_sel", 17),
	GATE_PERI(CLK_PERI_IRDA, "peri_irda", "irda_sel", 18),
	GATE_PERI(CLK_PERI_UART0, "peri_uart0", "axi_sel", 19),
	GATE_PERI(CLK_PERI_UART1, "peri_uart1", "axi_sel", 20),
	GATE_PERI(CLK_PERI_UART2, "peri_uart2", "axi_sel", 21),
	GATE_PERI(CLK_PERI_UART3, "peri_uart3", "axi_sel", 22),
	GATE_PERI(CLK_PERI_I2C0, "peri_i2c0", "axi_sel", 23),
	GATE_PERI(CLK_PERI_I2C1, "peri_i2c1", "axi_sel", 24),
	GATE_PERI(CLK_PERI_I2C2, "peri_i2c2", "axi_sel", 25),
	GATE_PERI(CLK_PERI_I2C3, "peri_i2c3", "axi_sel", 26),
	GATE_PERI(CLK_PERI_I2C4, "peri_i2c4", "axi_sel", 27),
	GATE_PERI(CLK_PERI_AUXADC, "peri_auxadc", "clk26m", 28),
	GATE_PERI(CLK_PERI_SPI0, "peri_spi0", "spi_sel", 29),
};

static u16 peri_rst_ofs[] = { 0x0 };

static u16 peri_idx_map[] = {
	[MT6795_PERI_NFI_SW_RST]   = 14,
	[MT6795_PERI_THERM_SW_RST] = 16,
	[MT6795_PERI_MSDC1_SW_RST] = 20,
};

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SIMPLE,
	.rst_bank_ofs = peri_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(peri_rst_ofs),
	.rst_idx_map = peri_idx_map,
	.rst_idx_map_nr = ARRAY_SIZE(peri_idx_map),
};

static const struct of_device_id of_match_clk_mt6795_pericfg[] = {
	{ .compatible = "mediatek,mt6795-pericfg" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6795_pericfg);

static int clk_mt6795_pericfg_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_PERI_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	ret = mtk_register_reset_controller_with_dev(&pdev->dev, &clk_rst_desc);
	if (ret)
		goto free_clk_data;

	ret = mtk_clk_register_gates(&pdev->dev, node, peri_gates,
				     ARRAY_SIZE(peri_gates), clk_data);
	if (ret)
		goto free_clk_data;

	ret = mtk_clk_register_composites(&pdev->dev, peri_clks,
					  ARRAY_SIZE(peri_clks), base,
					  &mt6795_peri_clk_lock, clk_data);
	if (ret)
		goto unregister_gates;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (ret)
		goto unregister_composites;

	return 0;

unregister_composites:
	mtk_clk_unregister_composites(peri_clks, ARRAY_SIZE(peri_clks), clk_data);
unregister_gates:
	mtk_clk_unregister_gates(peri_gates, ARRAY_SIZE(peri_gates), clk_data);
free_clk_data:
	mtk_free_clk_data(clk_data);
	return ret;
}

static void clk_mt6795_pericfg_remove(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);

	of_clk_del_provider(node);
	mtk_clk_unregister_composites(peri_clks, ARRAY_SIZE(peri_clks), clk_data);
	mtk_clk_unregister_gates(peri_gates, ARRAY_SIZE(peri_gates), clk_data);
	mtk_free_clk_data(clk_data);
}

static struct platform_driver clk_mt6795_pericfg_drv = {
	.driver = {
		.name = "clk-mt6795-pericfg",
		.of_match_table = of_match_clk_mt6795_pericfg,
	},
	.probe = clk_mt6795_pericfg_probe,
	.remove_new = clk_mt6795_pericfg_remove,
};
module_platform_driver(clk_mt6795_pericfg_drv);

MODULE_DESCRIPTION("MediaTek MT6795 pericfg clocks driver");
MODULE_LICENSE("GPL");
