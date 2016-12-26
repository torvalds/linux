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
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include "clk.h"
#include "clk-pll.h"

#define CLKEN_OV_VAL		0xf8
#define CPU_CLK_STATUS		0xfc
#define MISC_DOUT1		0x558

static void __iomem *reg_base;

/* parent clock name list */
PNAME(mout_armclk_p)	= { "cplla", "cpllb" };
PNAME(mout_spi_p)	= { "div125", "div200" };

/* fixed rate clocks generated outside the soc */
static struct samsung_fixed_rate_clock exynos5440_fixed_rate_ext_clks[] __initdata = {
	FRATE(0, "xtal", NULL, 0, 0),
};

/* fixed rate clocks */
static const struct samsung_fixed_rate_clock exynos5440_fixed_rate_clks[] __initconst = {
	FRATE(0, "ppll", NULL, 0, 1000000000),
	FRATE(0, "usb_phy0", NULL, 0, 60000000),
	FRATE(0, "usb_phy1", NULL, 0, 60000000),
	FRATE(0, "usb_ohci12", NULL, 0, 12000000),
	FRATE(0, "usb_ohci48", NULL, 0, 48000000),
};

/* fixed factor clocks */
static const struct samsung_fixed_factor_clock exynos5440_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "div250", "ppll", 1, 4, 0),
	FFACTOR(0, "div200", "ppll", 1, 5, 0),
	FFACTOR(0, "div125", "div250", 1, 2, 0),
};

/* mux clocks */
static const struct samsung_mux_clock exynos5440_mux_clks[] __initconst = {
	MUX(0, "mout_spi", mout_spi_p, MISC_DOUT1, 5, 1),
	MUX_A(CLK_ARM_CLK, "arm_clk", mout_armclk_p,
			CPU_CLK_STATUS, 0, 1, "armclk"),
};

/* divider clocks */
static const struct samsung_div_clock exynos5440_div_clks[] __initconst = {
	DIV(CLK_SPI_BAUD, "div_spi", "mout_spi", MISC_DOUT1, 3, 2),
};

/* gate clocks */
static const struct samsung_gate_clock exynos5440_gate_clks[] __initconst = {
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

static const struct of_device_id ext_clk_match[] __initconst = {
	{ .compatible = "samsung,clock-xtal", .data = (void *)0, },
	{},
};

static int exynos5440_clk_restart_notify(struct notifier_block *this,
		unsigned long code, void *unused)
{
	u32 val, status;

	status = readl_relaxed(reg_base + 0xbc);
	val = readl_relaxed(reg_base + 0xcc);
	val = (val & 0xffff0000) | (status & 0xffff);
	writel_relaxed(val, reg_base + 0xcc);

	return NOTIFY_DONE;
}

/*
 * Exynos5440 Clock restart notifier, handles restart functionality
 */
static struct notifier_block exynos5440_clk_restart_handler = {
	.notifier_call = exynos5440_clk_restart_notify,
	.priority = 128,
};

static const struct samsung_pll_clock exynos5440_plls[] __initconst = {
	PLL(pll_2550x, CLK_CPLLA, "cplla", "xtal", 0, 0x4c, NULL),
	PLL(pll_2550x, CLK_CPLLB, "cpllb", "xtal", 0, 0x50, NULL),
};

/* register exynos5440 clocks */
static void __init exynos5440_clk_init(struct device_node *np)
{
	struct samsung_clk_provider *ctx;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: failed to map clock controller registers,"
			" aborting clock initialization\n", __func__);
		return;
	}

	ctx = samsung_clk_init(np, reg_base, CLK_NR_CLKS);

	samsung_clk_of_register_fixed_ext(ctx, exynos5440_fixed_rate_ext_clks,
		ARRAY_SIZE(exynos5440_fixed_rate_ext_clks), ext_clk_match);

	samsung_clk_register_pll(ctx, exynos5440_plls,
			ARRAY_SIZE(exynos5440_plls), ctx->reg_base);

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

	if (register_restart_handler(&exynos5440_clk_restart_handler))
		pr_warn("exynos5440 clock can't register restart handler\n");

	pr_info("Exynos5440: arm_clk = %ldHz\n", _get_rate("arm_clk"));
	pr_info("exynos5440 clock initialization complete\n");
}
CLK_OF_DECLARE(exynos5440_clk, "samsung,exynos5440-clock", exynos5440_clk_init);
