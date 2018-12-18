/*
 * Copyright (C) 2013 Pengutronix
 * Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 */
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include <dt-bindings/clock/efm32-cmu.h>

#define CMU_HFPERCLKEN0		0x44
#define CMU_MAX_CLKS		37

static struct clk_hw_onecell_data *clk_data;

static void __init efm32gg_cmu_init(struct device_node *np)
{
	int i;
	void __iomem *base;
	struct clk_hw **hws;

	clk_data = kzalloc(struct_size(clk_data, hws, CMU_MAX_CLKS),
			   GFP_KERNEL);

	if (!clk_data)
		return;

	hws = clk_data->hws;

	for (i = 0; i < CMU_MAX_CLKS; ++i)
		hws[i] = ERR_PTR(-ENOENT);

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("Failed to map address range for efm32gg,cmu node\n");
		return;
	}

	hws[clk_HFXO] = clk_hw_register_fixed_rate(NULL, "HFXO", NULL, 0,
						   48000000);

	hws[clk_HFPERCLKUSART0] = clk_hw_register_gate(NULL, "HFPERCLK.USART0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 0, 0, NULL);
	hws[clk_HFPERCLKUSART1] = clk_hw_register_gate(NULL, "HFPERCLK.USART1",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 1, 0, NULL);
	hws[clk_HFPERCLKUSART2] = clk_hw_register_gate(NULL, "HFPERCLK.USART2",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 2, 0, NULL);
	hws[clk_HFPERCLKUART0] = clk_hw_register_gate(NULL, "HFPERCLK.UART0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 3, 0, NULL);
	hws[clk_HFPERCLKUART1] = clk_hw_register_gate(NULL, "HFPERCLK.UART1",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 4, 0, NULL);
	hws[clk_HFPERCLKTIMER0] = clk_hw_register_gate(NULL, "HFPERCLK.TIMER0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 5, 0, NULL);
	hws[clk_HFPERCLKTIMER1] = clk_hw_register_gate(NULL, "HFPERCLK.TIMER1",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 6, 0, NULL);
	hws[clk_HFPERCLKTIMER2] = clk_hw_register_gate(NULL, "HFPERCLK.TIMER2",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 7, 0, NULL);
	hws[clk_HFPERCLKTIMER3] = clk_hw_register_gate(NULL, "HFPERCLK.TIMER3",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 8, 0, NULL);
	hws[clk_HFPERCLKACMP0] = clk_hw_register_gate(NULL, "HFPERCLK.ACMP0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 9, 0, NULL);
	hws[clk_HFPERCLKACMP1] = clk_hw_register_gate(NULL, "HFPERCLK.ACMP1",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 10, 0, NULL);
	hws[clk_HFPERCLKI2C0] = clk_hw_register_gate(NULL, "HFPERCLK.I2C0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 11, 0, NULL);
	hws[clk_HFPERCLKI2C1] = clk_hw_register_gate(NULL, "HFPERCLK.I2C1",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 12, 0, NULL);
	hws[clk_HFPERCLKGPIO] = clk_hw_register_gate(NULL, "HFPERCLK.GPIO",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 13, 0, NULL);
	hws[clk_HFPERCLKVCMP] = clk_hw_register_gate(NULL, "HFPERCLK.VCMP",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 14, 0, NULL);
	hws[clk_HFPERCLKPRS] = clk_hw_register_gate(NULL, "HFPERCLK.PRS",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 15, 0, NULL);
	hws[clk_HFPERCLKADC0] = clk_hw_register_gate(NULL, "HFPERCLK.ADC0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 16, 0, NULL);
	hws[clk_HFPERCLKDAC0] = clk_hw_register_gate(NULL, "HFPERCLK.DAC0",
			"HFXO", 0, base + CMU_HFPERCLKEN0, 17, 0, NULL);

	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}
CLK_OF_DECLARE(efm32ggcmu, "efm32gg,cmu", efm32gg_cmu_init);
