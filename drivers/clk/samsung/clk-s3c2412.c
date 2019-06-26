/*
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for S3C2412 and S3C2413.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include <dt-bindings/clock/s3c2412.h>

#include "clk.h"
#include "clk-pll.h"

#define LOCKTIME	0x00
#define MPLLCON		0x04
#define UPLLCON		0x08
#define CLKCON		0x0c
#define CLKDIVN		0x14
#define CLKSRC		0x1c
#define SWRST		0x30

static void __iomem *reg_base;

/*
 * list of controller registers to be saved and restored during a
 * suspend/resume cycle.
 */
static unsigned long s3c2412_clk_regs[] __initdata = {
	LOCKTIME,
	MPLLCON,
	UPLLCON,
	CLKCON,
	CLKDIVN,
	CLKSRC,
};

static struct clk_div_table divxti_d[] = {
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

static struct samsung_div_clock s3c2412_dividers[] __initdata = {
	DIV_T(0, "div_xti", "xti", CLKSRC, 0, 3, divxti_d),
	DIV(0, "div_cam", "mux_cam", CLKDIVN, 16, 4),
	DIV(0, "div_i2s", "mux_i2s", CLKDIVN, 12, 4),
	DIV(0, "div_uart", "mux_uart", CLKDIVN, 8, 4),
	DIV(0, "div_usb", "mux_usb", CLKDIVN, 6, 1),
	DIV(0, "div_hclk_half", "hclk", CLKDIVN, 5, 1),
	DIV(ARMDIV, "armdiv", "msysclk", CLKDIVN, 3, 1),
	DIV(PCLK, "pclk", "hclk", CLKDIVN, 2, 1),
	DIV(HCLK, "hclk", "armdiv", CLKDIVN, 0, 2),
};

static struct samsung_fixed_factor_clock s3c2412_ffactor[] __initdata = {
	FFACTOR(0, "ff_hclk", "hclk", 2, 1, CLK_SET_RATE_PARENT),
};

/*
 * The first two use the OM[4] setting, which is not readable from
 * software, so assume it is set to xti.
 */
PNAME(erefclk_p) = { "xti", "xti", "xti", "ext" };
PNAME(urefclk_p) = { "xti", "xti", "xti", "ext" };

PNAME(camclk_p) = { "usysclk", "hclk" };
PNAME(usbclk_p) = { "usysclk", "hclk" };
PNAME(i2sclk_p) = { "erefclk", "mpll" };
PNAME(uartclk_p) = { "erefclk", "mpll" };
PNAME(usysclk_p) = { "urefclk", "upll" };
PNAME(msysclk_p) = { "mdivclk", "mpll" };
PNAME(mdivclk_p) = { "xti", "div_xti" };
PNAME(armclk_p) = { "armdiv", "hclk" };

static struct samsung_mux_clock s3c2412_muxes[] __initdata = {
	MUX(0, "erefclk", erefclk_p, CLKSRC, 14, 2),
	MUX(0, "urefclk", urefclk_p, CLKSRC, 12, 2),
	MUX(0, "mux_cam", camclk_p, CLKSRC, 11, 1),
	MUX(0, "mux_usb", usbclk_p, CLKSRC, 10, 1),
	MUX(0, "mux_i2s", i2sclk_p, CLKSRC, 9, 1),
	MUX(0, "mux_uart", uartclk_p, CLKSRC, 8, 1),
	MUX(USYSCLK, "usysclk", usysclk_p, CLKSRC, 5, 1),
	MUX(MSYSCLK, "msysclk", msysclk_p, CLKSRC, 4, 1),
	MUX(MDIVCLK, "mdivclk", mdivclk_p, CLKSRC, 3, 1),
	MUX(ARMCLK, "armclk", armclk_p, CLKDIVN, 4, 1),
};

static struct samsung_pll_clock s3c2412_plls[] __initdata = {
	PLL(pll_s3c2440_mpll, MPLL, "mpll", "xti", LOCKTIME, MPLLCON, NULL),
	PLL(pll_s3c2410_upll, UPLL, "upll", "urefclk", LOCKTIME, UPLLCON, NULL),
};

static struct samsung_gate_clock s3c2412_gates[] __initdata = {
	GATE(PCLK_WDT, "wdt", "pclk", CLKCON, 28, 0, 0),
	GATE(PCLK_SPI, "spi", "pclk", CLKCON, 27, 0, 0),
	GATE(PCLK_I2S, "i2s", "pclk", CLKCON, 26, 0, 0),
	GATE(PCLK_I2C, "i2c", "pclk", CLKCON, 25, 0, 0),
	GATE(PCLK_ADC, "adc", "pclk", CLKCON, 24, 0, 0),
	GATE(PCLK_RTC, "rtc", "pclk", CLKCON, 23, 0, 0),
	GATE(PCLK_GPIO, "gpio", "pclk", CLKCON, 22, CLK_IGNORE_UNUSED, 0),
	GATE(PCLK_UART2, "uart2", "pclk", CLKCON, 21, 0, 0),
	GATE(PCLK_UART1, "uart1", "pclk", CLKCON, 20, 0, 0),
	GATE(PCLK_UART0, "uart0", "pclk", CLKCON, 19, 0, 0),
	GATE(PCLK_SDI, "sdi", "pclk", CLKCON, 18, 0, 0),
	GATE(PCLK_PWM, "pwm", "pclk", CLKCON, 17, 0, 0),
	GATE(PCLK_USBD, "usb-device", "pclk", CLKCON, 16, 0, 0),
	GATE(SCLK_CAM, "sclk_cam", "div_cam", CLKCON, 15, 0, 0),
	GATE(SCLK_UART, "sclk_uart", "div_uart", CLKCON, 14, 0, 0),
	GATE(SCLK_I2S, "sclk_i2s", "div_i2s", CLKCON, 13, 0, 0),
	GATE(SCLK_USBH, "sclk_usbh", "div_usb", CLKCON, 12, 0, 0),
	GATE(SCLK_USBD, "sclk_usbd", "div_usb", CLKCON, 11, 0, 0),
	GATE(HCLK_HALF, "hclk_half", "div_hclk_half", CLKCON, 10, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_X2, "hclkx2", "ff_hclk", CLKCON, 9, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_SDRAM, "sdram", "hclk", CLKCON, 8, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_USBH, "usb-host", "hclk", CLKCON, 6, 0, 0),
	GATE(HCLK_LCD, "lcd", "hclk", CLKCON, 5, 0, 0),
	GATE(HCLK_NAND, "nand", "hclk", CLKCON, 4, 0, 0),
	GATE(HCLK_DMA3, "dma3", "hclk", CLKCON, 3, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_DMA2, "dma2", "hclk", CLKCON, 2, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_DMA1, "dma1", "hclk", CLKCON, 1, CLK_IGNORE_UNUSED, 0),
	GATE(HCLK_DMA0, "dma0", "hclk", CLKCON, 0, CLK_IGNORE_UNUSED, 0),
};

static struct samsung_clock_alias s3c2412_aliases[] __initdata = {
	ALIAS(PCLK_UART0, "s3c2412-uart.0", "uart"),
	ALIAS(PCLK_UART1, "s3c2412-uart.1", "uart"),
	ALIAS(PCLK_UART2, "s3c2412-uart.2", "uart"),
	ALIAS(PCLK_UART0, "s3c2412-uart.0", "clk_uart_baud2"),
	ALIAS(PCLK_UART1, "s3c2412-uart.1", "clk_uart_baud2"),
	ALIAS(PCLK_UART2, "s3c2412-uart.2", "clk_uart_baud2"),
	ALIAS(SCLK_UART, NULL, "clk_uart_baud3"),
	ALIAS(PCLK_I2C, "s3c2410-i2c.0", "i2c"),
	ALIAS(PCLK_ADC, NULL, "adc"),
	ALIAS(PCLK_RTC, NULL, "rtc"),
	ALIAS(PCLK_PWM, NULL, "timers"),
	ALIAS(HCLK_LCD, NULL, "lcd"),
	ALIAS(PCLK_USBD, NULL, "usb-device"),
	ALIAS(SCLK_USBD, NULL, "usb-bus-gadget"),
	ALIAS(HCLK_USBH, NULL, "usb-host"),
	ALIAS(SCLK_USBH, NULL, "usb-bus-host"),
	ALIAS(ARMCLK, NULL, "armclk"),
	ALIAS(HCLK, NULL, "hclk"),
	ALIAS(MPLL, NULL, "mpll"),
	ALIAS(MSYSCLK, NULL, "fclk"),
};

static int s3c2412_restart(struct notifier_block *this,
			   unsigned long mode, void *cmd)
{
	/* errata "Watch-dog/Software Reset Problem" specifies that
	 * this reset must be done with the SYSCLK sourced from
	 * EXTCLK instead of FOUT to avoid a glitch in the reset
	 * mechanism.
	 *
	 * See the watchdog section of the S3C2412 manual for more
	 * information on this fix.
	 */

	__raw_writel(0x00, reg_base + CLKSRC);
	__raw_writel(0x533C2412, reg_base + SWRST);
	return NOTIFY_DONE;
}

static struct notifier_block s3c2412_restart_handler = {
	.notifier_call = s3c2412_restart,
	.priority = 129,
};

/*
 * fixed rate clocks generated outside the soc
 * Only necessary until the devicetree-move is complete
 */
#define XTI	1
static struct samsung_fixed_rate_clock s3c2412_common_frate_clks[] __initdata = {
	FRATE(XTI, "xti", NULL, 0, 0),
	FRATE(0, "ext", NULL, 0, 0),
};

static void __init s3c2412_common_clk_register_fixed_ext(
		struct samsung_clk_provider *ctx,
		unsigned long xti_f, unsigned long ext_f)
{
	/* xtal alias is necessary for the current cpufreq driver */
	struct samsung_clock_alias xti_alias = ALIAS(XTI, NULL, "xtal");

	s3c2412_common_frate_clks[0].fixed_rate = xti_f;
	s3c2412_common_frate_clks[1].fixed_rate = ext_f;
	samsung_clk_register_fixed_rate(ctx, s3c2412_common_frate_clks,
				ARRAY_SIZE(s3c2412_common_frate_clks));

	samsung_clk_register_alias(ctx, &xti_alias, 1);
}

void __init s3c2412_common_clk_init(struct device_node *np, unsigned long xti_f,
				    unsigned long ext_f, void __iomem *base)
{
	struct samsung_clk_provider *ctx;
	int ret;
	reg_base = base;

	if (np) {
		reg_base = of_iomap(np, 0);
		if (!reg_base)
			panic("%s: failed to map registers\n", __func__);
	}

	ctx = samsung_clk_init(np, reg_base, NR_CLKS);

	/* Register external clocks only in non-dt cases */
	if (!np)
		s3c2412_common_clk_register_fixed_ext(ctx, xti_f, ext_f);

	/* Register PLLs. */
	samsung_clk_register_pll(ctx, s3c2412_plls, ARRAY_SIZE(s3c2412_plls),
				 reg_base);

	/* Register common internal clocks. */
	samsung_clk_register_mux(ctx, s3c2412_muxes, ARRAY_SIZE(s3c2412_muxes));
	samsung_clk_register_div(ctx, s3c2412_dividers,
					  ARRAY_SIZE(s3c2412_dividers));
	samsung_clk_register_gate(ctx, s3c2412_gates,
					ARRAY_SIZE(s3c2412_gates));
	samsung_clk_register_fixed_factor(ctx, s3c2412_ffactor,
					  ARRAY_SIZE(s3c2412_ffactor));
	samsung_clk_register_alias(ctx, s3c2412_aliases,
				   ARRAY_SIZE(s3c2412_aliases));

	samsung_clk_sleep_init(reg_base, s3c2412_clk_regs,
			       ARRAY_SIZE(s3c2412_clk_regs));

	samsung_clk_of_add_provider(np, ctx);

	ret = register_restart_handler(&s3c2412_restart_handler);
	if (ret)
		pr_warn("cannot register restart handler, %d\n", ret);
}

static void __init s3c2412_clk_init(struct device_node *np)
{
	s3c2412_common_clk_init(np, 0, 0, NULL);
}
CLK_OF_DECLARE(s3c2412_clk, "samsung,s3c2412-clock", s3c2412_clk_init);
