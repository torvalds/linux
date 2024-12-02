// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/marvell,pxa1908.h>

#include "clk.h"

#define APBCP_UART2		0x1c
#define APBCP_TWSI2		0x28
#define APBCP_AICER		0x38

#define APBCP_NR_CLKS		4

struct pxa1908_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *base;
};

static DEFINE_SPINLOCK(uart2_lock);

static const char * const uart_parent_names[] = {"pll1_117", "uart_pll"};

static struct mmp_param_gate_clk apbcp_gate_clks[] = {
	{PXA1908_CLK_UART2, "uart2_clk", "uart2_mux", CLK_SET_RATE_PARENT, APBCP_UART2, 0x7, 0x3, 0x0, 0, &uart2_lock},
	{PXA1908_CLK_TWSI2, "twsi2_clk", "pll1_32", CLK_SET_RATE_PARENT, APBCP_TWSI2, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1908_CLK_AICER, "ripc_clk", NULL, 0, APBCP_AICER, 0x7, 0x2, 0x0, 0, NULL},
};

static struct mmp_param_mux_clk apbcp_mux_clks[] = {
	{0, "uart2_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBCP_UART2, 4, 3, 0, &uart2_lock},
};

static void pxa1908_apb_p_periph_clk_init(struct pxa1908_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_mux_clks(unit, apbcp_mux_clks, pxa_unit->base,
			ARRAY_SIZE(apbcp_mux_clks));
	mmp_register_gate_clks(unit, apbcp_gate_clks, pxa_unit->base,
			ARRAY_SIZE(apbcp_gate_clks));
}

static int pxa1908_apbcp_probe(struct platform_device *pdev)
{
	struct pxa1908_clk_unit *pxa_unit;

	pxa_unit = devm_kzalloc(&pdev->dev, sizeof(*pxa_unit), GFP_KERNEL);
	if (IS_ERR(pxa_unit))
		return PTR_ERR(pxa_unit);

	pxa_unit->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pxa_unit->base))
		return PTR_ERR(pxa_unit->base);

	mmp_clk_init(pdev->dev.of_node, &pxa_unit->unit, APBCP_NR_CLKS);

	pxa1908_apb_p_periph_clk_init(pxa_unit);

	return 0;
}

static const struct of_device_id pxa1908_apbcp_match_table[] = {
	{ .compatible = "marvell,pxa1908-apbcp" },
	{ }
};
MODULE_DEVICE_TABLE(of, pxa1908_apbcp_match_table);

static struct platform_driver pxa1908_apbcp_driver = {
	.probe = pxa1908_apbcp_probe,
	.driver = {
		.name = "pxa1908-apbcp",
		.of_match_table = pxa1908_apbcp_match_table
	}
};
module_platform_driver(pxa1908_apbcp_driver);

MODULE_AUTHOR("Duje Mihanović <duje.mihanovic@skole.hr>");
MODULE_DESCRIPTION("Marvell PXA1908 APBCP Clock Driver");
MODULE_LICENSE("GPL");
