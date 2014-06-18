/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos5440 SoC.
*/

#include <dt-bindings/clock/exynos5440.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"
#include "clk-pll.h"

#define CLKEN_OV_VAL		0xf8
#define CPU_CLK_STATUS		0xfc
#define MISC_DOUT1		0x558

/* parent clock name list */
PNAME(mout_armclk_p)	= { "cplla", "cpllb" };
PNAME(mout_spi_p)	= { "div125", "div200" };

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock exynos5440_fixed_rate_ext_clks[] __initdata = {
	FRATE(0, "xtal", NULL, CLK_IS_ROOT, 0),
};

/* fixed rate clocks */
static struct samsung_fixed_rate_clock exynos5440_fixed_rate_clks[] __initdata = {
	FRATE(0, "ppll", NULL, CLK_IS_ROOT, 1000000000),
	FRATE(0, "usb_phy0", NULL, CLK_IS_ROOT, 60000000),
	FRATE(0, "usb_phy1", NULL, CLK_IS_ROOT, 60000000),
	FRATE(0, "usb_ohci12", NULL, CLK_IS_ROOT, 12000000),
	FRATE(0, "usb_ohci48", NULL, CLK_IS_ROOT, 48000000),
};

/* fixed factor clocks */
static struct samsung_fixed_factor_clock exynos5440_fixed_factor_clks[] __initdata = {
	FFACTOR(0, "div250", "ppll", 1, 4, 0),
	FFACTOR(0, "div200", "ppll", 1, 5, 0),
	FFACTOR(0, "div125", "div250", 1, 2, 0),
};

/* mux clocks */
static struct samsung_mux_clock exynos5440_mux_clks[] __initdata = {
	MUX(0, "mout_spi", mout_spi_p, MISC_DOUT1, 5, 1),
	MUX_A(CLK_ARM_CLK, "arm_clk", mout_armclk_p,
			CPU_CLK_STATUS, 0, 1, "armclk"),
};

/* divider clocks */
static struct samsung_div_clock exynos5440_div_clks[] __initdata = {
	DIV(CLK_SPI_BAUD, "div_spi", "mout_spi", MISC_DOUT1, 3, 2),
};

/* gate clocks */
static struct samsung_gate_clock exynos5440_gate_clks[] __initdata = {
	GATE(CLK_PB0_250, "pb0_250", "div250", CLKEN_OV_VAL, 3, 0, 0),
	GATE(CLK_PR0_250, "pr0_250", "div250", CLKEN_OV_VAL, 4, 0, 0),
	GATE(CLK_PR1_250, "pr1_250", "div250", CLKEN_OV_VAL, 5, 0, 0),
	GATE(CLK_B_250, "b_250", "div250", CLKEN_OV_VAL, 9, 0, 0),
	GATE(CLK_B_125, "b_125", "div125", CLKEN_OV_VAL, 10, 0, 0),
	GATE(CLK_B_200, "b_200", "div200", CLKEN_OV_VAL, 11, 0, 0),
	GATE(CLK_SATA, "sata", "div200", CLKEN_OV_VAL, 12, 0, 0),
	GATE(CLK_USB, "usb", "div200", CLKEN_OV_VAL, 13, 0, 0),
	GATE(CLK_GMAC0, "gmac0", "div200", CLKEN_OV_VAL, 14, 0, 0),
	GATE(CLK_CS250, "cs250", "div250", CLKEN_OV_VAL, 19, 0, 0),
	GATE(CLK_PB0_250_O, "pb0_250_o", "pb0_250", CLKEN_OV_VAL, 3, 0, 0),
	GATE(CLK_PR0_250_O, "pr0_250_o", "pr0_250", CLKEN_OV_VAL, 4, 0, 0),
	GATE(CLK_PR1_250_O, "pr1_250_o", "pr1_250", CLKEN_OV_VAL, 5, 0, 0),
	GATE(CLK_B_250_O, "b_250_o", "b_250", CLKEN_OV_VAL, 9, 0, 0),
	GATE(CLK_B_125_O, "b_125_o", "b_125", CLKEN_OV_VAL, 10, 0, 0),
	GATE(CLK_B_200_O, "b_200_o", "b_200", CLKEN_OV_VAL, 11, 0, 0),
	GATE(CLK_SATA_O, "sata_o", "sata", CLKEN_OV_VAL, 12, 0, 0),
	GATE(CLK_USB_O, "usb_o", "usb", CLKEN_OV_VAL, 13, 0, 0),
	GATE(CLK_GMAC0_O, "gmac0_o", "gmac", CLKEN_OV_VAL, 14, 0, 0),
	GATE(CLK_CS250_O, "cs250_o", "cs250", CLKEN_OV_VAL, 19, 0, 0),
};

static struct of_device_id ext_clk_match[] __initdata = {
	{ .compatible = "samsung,clock-xtal", .data = (void *)0, },
	{},
};

/* register exynos5440 clocks */
static void __init exynos5440_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct samsung_clk_provider *ctx;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: failed to map clock controller registers,"
			" aborting clock initialization\n", __func__);
		return;
	}

	ctx = samsung_clk_init(np, reg_base, CLK_NR_CLKS);
	if (!ctx)
		panic("%s: unable to allocate context.\n", __func__);

	samsung_clk_of_register_fixed_ext(ctx, exynos5440_fixed_rate_ext_clks,
		ARRAY_SIZE(exynos5440_fixed_rate_ext_clks), ext_clk_match);

	samsung_clk_register_pll2550x("cplla", "xtal", reg_base + 0x1c, 0x10);
	samsung_clk_register_pll2550x("cpllb", "xtal", reg_base + 0x20, 0x10);

	samsung_clk_register_fixed_rate(ctx, exynos5440_fixed_rate_clks,
			ARRAY_SIZE(exynos5440_fixed_rate_clks));
	samsung_clk_register_fixed_factor(ctx, exynos5440_fixed_factor_clks,
			ARRAY_SIZE(exynos5440_fixed_factor_clks));
	samsung_clk_register_mux(ctx, exynos5440_mux_clks,
			ARRAY_SIZE(exynos5440_mux_clks));
	samsung_clk_register_div(ctx, exynos5440_div_clks,
			ARRAY_SIZE(exynos5440_div_clks));
	samsung_clk_register_gate(ctx, exynos5440_gate_clks,
			ARRAY_SIZE(exynos5440_gate_clks));

	samsung_clk_of_add_provider(np, ctx);

	pr_info("Exynos5440: arm_clk = %ldHz\n", _get_rate("arm_clk"));
	pr_info("exynos5440 clock initialization complete\n");
}
CLK_OF_DECLARE(exynos5440_clk, "samsung,exynos5440-clock", exynos5440_clk_init);
