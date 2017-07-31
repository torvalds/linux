/*
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for S3C2410 and following SoCs.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/s3c2410.h>

#include "clk.h"
#include "clk-pll.h"

#define LOCKTIME	0x00
#define MPLLCON		0x04
#define UPLLCON		0x08
#define CLKCON		0x0c
#define CLKSLOW		0x10
#define CLKDIVN		0x14
#define CAMDIVN		0x18

/* the soc types */
enum supported_socs {
	S3C2410,
	S3C2440,
	S3C2442,
};

/* list of PLLs to be registered */
enum s3c2410_plls {
	mpll, upll,
};

static void __iomem *reg_base;

#ifdef CONFIG_PM_SLEEP
static struct samsung_clk_reg_dump *s3c2410_save;

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static unsigned long s3c2410_clk_regs[] __initdata = {
	LOCKTIME,
	MPLLCON,
	UPLLCON,
	CLKCON,
	CLKSLOW,
	CLKDIVN,
	CAMDIVN,
};

static int s3c2410_clk_suspend(void)
{
	samsung_clk_save(reg_base, s3c2410_save,
				ARRAY_SIZE(s3c2410_clk_regs));

	return 0;
}

static void s3c2410_clk_resume(void)
{
	samsung_clk_restore(reg_base, s3c2410_save,
				ARRAY_SIZE(s3c2410_clk_regs));
}

static struct syscore_ops s3c2410_clk_syscore_ops = {
	.suspend = s3c2410_clk_suspend,
	.resume = s3c2410_clk_resume,
};

static void __init s3c2410_clk_sleep_init(void)
{
	s3c2410_save = samsung_clk_alloc_reg_dump(s3c2410_clk_regs,
						ARRAY_SIZE(s3c2410_clk_regs));
	if (!s3c2410_save) {
		pr_warn("%s: failed to allocate sleep save data, no sleep support!\n",
			__func__);
		return;
	}

	register_syscore_ops(&s3c2410_clk_syscore_ops);
	return;
}
#else
static void __init s3c2410_clk_sleep_init(void) {}
#endif

PNAME(fclk_p) = { "mpll", "div_slow" };

struct samsung_mux_clock s3c2410_common_muxes[] __initdata = {
	MUX(FCLK, "fclk", fclk_p, CLKSLOW, 4, 1),
};

static struct clk_div_table divslow_d[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 6 },
	{ .val = 4, .div = 8 },
	{ .val = 5, .div = 10 },
	{ .val = 6, .div = 12 },
	{ .val = 7, .div = 14 },
	{ /* sentinel */ },
};

struct samsung_div_clock s3c2410_common_dividers[] __initdata = {
	DIV_T(0, "div_slow", "xti", CLKSLOW, 0, 3, divslow_d),
	DIV(PCLK, "pclk", "hclk", CLKDIVN, 0, 1),
};

struct samsung_gate_clock s3c2410_common_gates[] __initdata = {
	GATE(PCLK_SPI, "spi", "pclk", CLKCON, 18, 0, 0),
	GATE(PCLK_I2S, "i2s", "pclk", CLKCON, 17, 0, 0),
	GATE(PCLK_I2C, "i2c", "pclk", CLKCON, 16, 0, 0),
	GATE(PCLK_ADC, "adc", "pclk", CLKCON, 15, 0, 0),
	GATE(PCLK_RTC, "rtc", "pclk", CLKCON, 14, 0, 0),
	GATE(PCLK_GPIO, "gpio", "pclk", CLKCON, 13, CLK_IGNORE_UNUSED, 0),
	GATE(PCLK_UART2, "uart2", "pclk", CLKCON, 12, 0, 0),
	GATE(PCLK_UART1, "uart1", "pclk", CLKCON, 11, 0, 0),
	GATE(PCLK_UART0, "uart0", "pclk", CLKCON, 10, 0, 0),
	GATE(PCLK_SDI, "sdi", "pclk", CLKCON, 9, 0, 0),
	GATE(PCLK_PWM, "pwm", "pclk", CLKCON, 8, 0, 0),
	GATE(HCLK_USBD, "usb-device", "hclk", CLKCON, 7, 0, 0),
	GATE(HCLK_USBH, "usb-host", "hclk", CLKCON, 6, 0, 0),
	GATE(HCLK_LCD, "lcd", "hclk", CLKCON, 5, 0, 0),
	GATE(HCLK_NAND, "nand", "hclk", CLKCON, 4, 0, 0),
};

/* should be added _after_ the soc-specific clocks are created */
struct samsung_clock_alias s3c2410_common_aliases[] __initdata = {
	ALIAS(PCLK_I2C, "s3c2410-i2c.0", "i2c"),
	ALIAS(PCLK_ADC, NULL, "adc"),
	ALIAS(PCLK_RTC, NULL, "rtc"),
	ALIAS(PCLK_PWM, NULL, "timers"),
	ALIAS(HCLK_LCD, NULL, "lcd"),
	ALIAS(HCLK_USBD, NULL, "usb-device"),
	ALIAS(HCLK_USBH, NULL, "usb-host"),
	ALIAS(UCLK, NULL, "usb-bus-host"),
	ALIAS(UCLK, NULL, "usb-bus-gadget"),
	ALIAS(ARMCLK, NULL, "armclk"),
	ALIAS(UCLK, NULL, "uclk"),
	ALIAS(HCLK, NULL, "hclk"),
	ALIAS(MPLL, NULL, "mpll"),
	ALIAS(FCLK, NULL, "fclk"),
	ALIAS(PCLK, NULL, "watchdog"),
	ALIAS(PCLK_SDI, NULL, "sdi"),
	ALIAS(HCLK_NAND, NULL, "nand"),
	ALIAS(PCLK_I2S, NULL, "iis"),
	ALIAS(PCLK_I2C, NULL, "i2c"),
};

/* S3C2410 specific clocks */

static struct samsung_pll_rate_table pll_s3c2410_12mhz_tbl[] __initdata = {
	/* sorted in descending order */
	/* 2410A extras */
	PLL_35XX_RATE(270000000, 127, 1, 1),
	PLL_35XX_RATE(268000000, 126, 1, 1),
	PLL_35XX_RATE(266000000, 125, 1, 1),
	PLL_35XX_RATE(226000000, 105, 1, 1),
	PLL_35XX_RATE(210000000, 132, 2, 1),
	/* 2410 common */
	PLL_35XX_RATE(203000000, 161, 3, 1),
	PLL_35XX_RATE(192000000, 88, 1, 1),
	PLL_35XX_RATE(186000000, 85, 1, 1),
	PLL_35XX_RATE(180000000, 82, 1, 1),
	PLL_35XX_RATE(170000000, 77, 1, 1),
	PLL_35XX_RATE(158000000, 71, 1, 1),
	PLL_35XX_RATE(152000000, 68, 1, 1),
	PLL_35XX_RATE(147000000, 90, 2, 1),
	PLL_35XX_RATE(135000000, 82, 2, 1),
	PLL_35XX_RATE(124000000, 116, 1, 2),
	PLL_35XX_RATE(118000000, 150, 2, 2),
	PLL_35XX_RATE(113000000, 105, 1, 2),
	PLL_35XX_RATE(101000000, 127, 2, 2),
	PLL_35XX_RATE(90000000, 112, 2, 2),
	PLL_35XX_RATE(85000000, 105, 2, 2),
	PLL_35XX_RATE(79000000, 71, 1, 2),
	PLL_35XX_RATE(68000000, 82, 2, 2),
	PLL_35XX_RATE(56000000, 142, 2, 3),
	PLL_35XX_RATE(48000000, 120, 2, 3),
	PLL_35XX_RATE(51000000, 161, 3, 3),
	PLL_35XX_RATE(45000000, 82, 1, 3),
	PLL_35XX_RATE(34000000, 82, 2, 3),
	{ /* sentinel */ },
};

static struct samsung_pll_clock s3c2410_plls[] __initdata = {
	[mpll] = PLL(pll_s3c2410_mpll, MPLL, "mpll", "xti",
						LOCKTIME, MPLLCON, NULL),
	[upll] = PLL(pll_s3c2410_upll, UPLL, "upll", "xti",
						LOCKTIME, UPLLCON, NULL),
};

struct samsung_div_clock s3c2410_dividers[] __initdata = {
	DIV(HCLK, "hclk", "mpll", CLKDIVN, 1, 1),
};

struct samsung_fixed_factor_clock s3c2410_ffactor[] __initdata = {
	/*
	 * armclk is directly supplied by the fclk, without
	 * switching possibility like on the s3c244x below.
	 */
	FFACTOR(ARMCLK, "armclk", "fclk", 1, 1, 0),

	/* uclk is fed from the unmodified upll */
	FFACTOR(UCLK, "uclk", "upll", 1, 1, 0),
};

struct samsung_clock_alias s3c2410_aliases[] __initdata = {
	ALIAS(PCLK_UART0, "s3c2410-uart.0", "uart"),
	ALIAS(PCLK_UART1, "s3c2410-uart.1", "uart"),
	ALIAS(PCLK_UART2, "s3c2410-uart.2", "uart"),
	ALIAS(PCLK_UART0, "s3c2410-uart.0", "clk_uart_baud0"),
	ALIAS(PCLK_UART1, "s3c2410-uart.1", "clk_uart_baud0"),
	ALIAS(PCLK_UART2, "s3c2410-uart.2", "clk_uart_baud0"),
	ALIAS(UCLK, NULL, "clk_uart_baud1"),
};

/* S3C244x specific clocks */

static struct samsung_pll_rate_table pll_s3c244x_12mhz_tbl[] __initdata = {
	/* sorted in descending order */
	PLL_35XX_RATE(400000000, 0x5c, 1, 1),
	PLL_35XX_RATE(390000000, 0x7a, 2, 1),
	PLL_35XX_RATE(380000000, 0x57, 1, 1),
	PLL_35XX_RATE(370000000, 0xb1, 4, 1),
	PLL_35XX_RATE(360000000, 0x70, 2, 1),
	PLL_35XX_RATE(350000000, 0xa7, 4, 1),
	PLL_35XX_RATE(340000000, 0x4d, 1, 1),
	PLL_35XX_RATE(330000000, 0x66, 2, 1),
	PLL_35XX_RATE(320000000, 0x98, 4, 1),
	PLL_35XX_RATE(310000000, 0x93, 4, 1),
	PLL_35XX_RATE(300000000, 0x75, 3, 1),
	PLL_35XX_RATE(240000000, 0x70, 1, 2),
	PLL_35XX_RATE(230000000, 0x6b, 1, 2),
	PLL_35XX_RATE(220000000, 0x66, 1, 2),
	PLL_35XX_RATE(210000000, 0x84, 2, 2),
	PLL_35XX_RATE(200000000, 0x5c, 1, 2),
	PLL_35XX_RATE(190000000, 0x57, 1, 2),
	PLL_35XX_RATE(180000000, 0x70, 2, 2),
	PLL_35XX_RATE(170000000, 0x4d, 1, 2),
	PLL_35XX_RATE(160000000, 0x98, 4, 2),
	PLL_35XX_RATE(150000000, 0x75, 3, 2),
	PLL_35XX_RATE(120000000, 0x70, 1, 3),
	PLL_35XX_RATE(110000000, 0x66, 1, 3),
	PLL_35XX_RATE(100000000, 0x5c, 1, 3),
	PLL_35XX_RATE(90000000, 0x70, 2, 3),
	PLL_35XX_RATE(80000000, 0x98, 4, 3),
	PLL_35XX_RATE(75000000, 0x75, 3, 3),
	{ /* sentinel */ },
};

static struct samsung_pll_clock s3c244x_common_plls[] __initdata = {
	[mpll] = PLL(pll_s3c2440_mpll, MPLL, "mpll", "xti",
						LOCKTIME, MPLLCON, NULL),
	[upll] = PLL(pll_s3c2410_upll, UPLL, "upll", "xti",
						LOCKTIME, UPLLCON, NULL),
};

PNAME(hclk_p) = { "fclk", "div_hclk_2", "div_hclk_4", "div_hclk_3" };
PNAME(armclk_p) = { "fclk", "hclk" };

struct samsung_mux_clock s3c244x_common_muxes[] __initdata = {
	MUX(HCLK, "hclk", hclk_p, CLKDIVN, 1, 2),
	MUX(ARMCLK, "armclk", armclk_p, CAMDIVN, 12, 1),
};

struct samsung_fixed_factor_clock s3c244x_common_ffactor[] __initdata = {
	FFACTOR(0, "div_hclk_2", "fclk", 1, 2, 0),
	FFACTOR(0, "ff_cam", "div_cam", 2, 1, CLK_SET_RATE_PARENT),
};

static struct clk_div_table div_hclk_4_d[] = {
	{ .val = 0, .div = 4 },
	{ .val = 1, .div = 8 },
	{ /* sentinel */ },
};

static struct clk_div_table div_hclk_3_d[] = {
	{ .val = 0, .div = 3 },
	{ .val = 1, .div = 6 },
	{ /* sentinel */ },
};

struct samsung_div_clock s3c244x_common_dividers[] __initdata = {
	DIV(UCLK, "uclk", "upll", CLKDIVN, 3, 1),
	DIV(0, "div_hclk", "fclk", CLKDIVN, 1, 1),
	DIV_T(0, "div_hclk_4", "fclk", CAMDIVN, 9, 1, div_hclk_4_d),
	DIV_T(0, "div_hclk_3", "fclk", CAMDIVN, 8, 1, div_hclk_3_d),
	DIV(0, "div_cam", "upll", CAMDIVN, 0, 3),
};

struct samsung_gate_clock s3c244x_common_gates[] __initdata = {
	GATE(HCLK_CAM, "cam", "hclk", CLKCON, 19, 0, 0),
};

struct samsung_clock_alias s3c244x_common_aliases[] __initdata = {
	ALIAS(PCLK_UART0, "s3c2440-uart.0", "uart"),
	ALIAS(PCLK_UART1, "s3c2440-uart.1", "uart"),
	ALIAS(PCLK_UART2, "s3c2440-uart.2", "uart"),
	ALIAS(PCLK_UART0, "s3c2440-uart.0", "clk_uart_baud2"),
	ALIAS(PCLK_UART1, "s3c2440-uart.1", "clk_uart_baud2"),
	ALIAS(PCLK_UART2, "s3c2440-uart.2", "clk_uart_baud2"),
	ALIAS(HCLK_CAM, NULL, "camif"),
	ALIAS(CAMIF, NULL, "camif-upll"),
};

/* S3C2440 specific clocks */

PNAME(s3c2440_camif_p) = { "upll", "ff_cam" };

struct samsung_mux_clock s3c2440_muxes[] __initdata = {
	MUX(CAMIF, "camif", s3c2440_camif_p, CAMDIVN, 4, 1),
};

struct samsung_gate_clock s3c2440_gates[] __initdata = {
	GATE(PCLK_AC97, "ac97", "pclk", CLKCON, 20, 0, 0),
};

/* S3C2442 specific clocks */

struct samsung_fixed_factor_clock s3c2442_ffactor[] __initdata = {
	FFACTOR(0, "upll_3", "upll", 1, 3, 0),
};

PNAME(s3c2442_camif_p) = { "upll", "ff_cam", "upll", "upll_3" };

struct samsung_mux_clock s3c2442_muxes[] __initdata = {
	MUX(CAMIF, "camif", s3c2442_camif_p, CAMDIVN, 4, 2),
};

/*
 * fixed rate clocks generated outside the soc
 * Only necessary until the devicetree-move is complete
 */
#define XTI	1
struct samsung_fixed_rate_clock s3c2410_common_frate_clks[] __initdata = {
	FRATE(XTI, "xti", NULL, 0, 0),
};

static void __init s3c2410_common_clk_register_fixed_ext(
		struct samsung_clk_provider *ctx,
		unsigned long xti_f)
{
	struct samsung_clock_alias xti_alias = ALIAS(XTI, NULL, "xtal");

	s3c2410_common_frate_clks[0].fixed_rate = xti_f;
	samsung_clk_register_fixed_rate(ctx, s3c2410_common_frate_clks,
				ARRAY_SIZE(s3c2410_common_frate_clks));

	samsung_clk_register_alias(ctx, &xti_alias, 1);
}

void __init s3c2410_common_clk_init(struct device_node *np, unsigned long xti_f,
				    int current_soc,
				    void __iomem *base)
{
	struct samsung_clk_provider *ctx;
	reg_base = base;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	}

	ctx = samsung_clk_init(np, reg_base, NR_CLKS);

	/* Register external clocks only in non-dt cases */
	if (!np)
		s3c2410_common_clk_register_fixed_ext(ctx, xti_f);

	if (current_soc == S3C2410) {
		if (_get_rate("xti") == 12 * MHZ) {
			s3c2410_plls[mpll].rate_table = pll_s3c2410_12mhz_tbl;
			s3c2410_plls[upll].rate_table = pll_s3c2410_12mhz_tbl;
		}

		/* Register PLLs. */
		samsung_clk_register_pll(ctx, s3c2410_plls,
				ARRAY_SIZE(s3c2410_plls), reg_base);

	} else { /* S3C2440, S3C2442 */
		if (_get_rate("xti") == 12 * MHZ) {
			/*
			 * plls follow different calculation schemes, with the
			 * upll following the same scheme as the s3c2410 plls
			 */
			s3c244x_common_plls[mpll].rate_table =
							pll_s3c244x_12mhz_tbl;
			s3c244x_common_plls[upll].rate_table =
							pll_s3c2410_12mhz_tbl;
		}

		/* Register PLLs. */
		samsung_clk_register_pll(ctx, s3c244x_common_plls,
				ARRAY_SIZE(s3c244x_common_plls), reg_base);
	}

	/* Register common internal clocks. */
	samsung_clk_register_mux(ctx, s3c2410_common_muxes,
			ARRAY_SIZE(s3c2410_common_muxes));
	samsung_clk_register_div(ctx, s3c2410_common_dividers,
			ARRAY_SIZE(s3c2410_common_dividers));
	samsung_clk_register_gate(ctx, s3c2410_common_gates,
		ARRAY_SIZE(s3c2410_common_gates));

	if (current_soc == S3C2440 || current_soc == S3C2442) {
		samsung_clk_register_div(ctx, s3c244x_common_dividers,
				ARRAY_SIZE(s3c244x_common_dividers));
		samsung_clk_register_gate(ctx, s3c244x_common_gates,
				ARRAY_SIZE(s3c244x_common_gates));
		samsung_clk_register_mux(ctx, s3c244x_common_muxes,
				ARRAY_SIZE(s3c244x_common_muxes));
		samsung_clk_register_fixed_factor(ctx, s3c244x_common_ffactor,
				ARRAY_SIZE(s3c244x_common_ffactor));
	}

	/* Register SoC-specific clocks. */
	switch (current_soc) {
	case S3C2410:
		samsung_clk_register_div(ctx, s3c2410_dividers,
				ARRAY_SIZE(s3c2410_dividers));
		samsung_clk_register_fixed_factor(ctx, s3c2410_ffactor,
				ARRAY_SIZE(s3c2410_ffactor));
		samsung_clk_register_alias(ctx, s3c2410_aliases,
			ARRAY_SIZE(s3c2410_aliases));
		break;
	case S3C2440:
		samsung_clk_register_mux(ctx, s3c2440_muxes,
				ARRAY_SIZE(s3c2440_muxes));
		samsung_clk_register_gate(ctx, s3c2440_gates,
				ARRAY_SIZE(s3c2440_gates));
		break;
	case S3C2442:
		samsung_clk_register_mux(ctx, s3c2442_muxes,
				ARRAY_SIZE(s3c2442_muxes));
		samsung_clk_register_fixed_factor(ctx, s3c2442_ffactor,
				ARRAY_SIZE(s3c2442_ffactor));
		break;
	}

	/*
	 * Register common aliases at the end, as some of the aliased clocks
	 * are SoC specific.
	 */
	samsung_clk_register_alias(ctx, s3c2410_common_aliases,
		ARRAY_SIZE(s3c2410_common_aliases));

	if (current_soc == S3C2440 || current_soc == S3C2442) {
		samsung_clk_register_alias(ctx, s3c244x_common_aliases,
			ARRAY_SIZE(s3c244x_common_aliases));
	}

	s3c2410_clk_sleep_init();

	samsung_clk_of_add_provider(np, ctx);
}

static void __init s3c2410_clk_init(struct device_node *np)
{
	s3c2410_common_clk_init(np, 0, S3C2410, 0);
}
CLK_OF_DECLARE(s3c2410_clk, "samsung,s3c2410-clock", s3c2410_clk_init);

static void __init s3c2440_clk_init(struct device_node *np)
{
	s3c2410_common_clk_init(np, 0, S3C2440, 0);
}
CLK_OF_DECLARE(s3c2440_clk, "samsung,s3c2440-clock", s3c2440_clk_init);

static void __init s3c2442_clk_init(struct device_node *np)
{
	s3c2410_common_clk_init(np, 0, S3C2442, 0);
}
CLK_OF_DECLARE(s3c2442_clk, "samsung,s3c2442-clock", s3c2442_clk_init);
