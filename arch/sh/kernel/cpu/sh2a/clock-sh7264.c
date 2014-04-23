/*
 * arch/sh/kernel/cpu/sh2a/clock-sh7264.c
 *
 * SH7264 clock framework support
 *
 * Copyright (C) 2012  Phil Edworthy
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>

/* SH7264 registers */
#define FRQCR		0xfffe0010
#define STBCR3		0xfffe0408
#define STBCR4		0xfffe040c
#define STBCR5		0xfffe0410
#define STBCR6		0xfffe0414
#define STBCR7		0xfffe0418
#define STBCR8		0xfffe041c

static const unsigned int pll1rate[] = {8, 12};

static unsigned int pll1_div;

/* Fixed 32 KHz root clock for RTC */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 18000000,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long rate = clk->parent->rate / pll1_div;
	return rate * pll1rate[(__raw_readw(FRQCR) >> 8) & 1];
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&pll_clk,
};

static int div2[] = { 1, 2, 3, 4, 6, 8, 12 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_P,
       DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

/* The mask field specifies the div2 entries that are valid */
struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCR, 4, 0x7,  CLK_ENABLE_REG_16BIT
					| CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4(FRQCR, 0, 0x78, CLK_ENABLE_REG_16BIT),
};

enum {	MSTP77, MSTP74, MSTP72,
	MSTP60,
	MSTP35, MSTP34, MSTP33, MSTP32, MSTP30,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP77] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR7, 7, 0), /* SCIF */
	[MSTP74] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR7, 4, 0), /* VDC */
	[MSTP72] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR7, 2, 0), /* CMT */
	[MSTP60] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR6, 0, 0), /* USB */
	[MSTP35] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR3, 6, 0), /* MTU2 */
	[MSTP34] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR3, 4, 0), /* SDHI0 */
	[MSTP33] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR3, 3, 0), /* SDHI1 */
	[MSTP32] = SH_CLK_MSTP8(&div4_clks[DIV4_P], STBCR3, 2, 0), /* ADC */
	[MSTP30] = SH_CLK_MSTP8(&r_clk, STBCR3, 0, 0),	/* RTC */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),

	/* MSTP clocks */
	CLKDEV_CON_ID("sci_ick", &mstp_clks[MSTP77]),
	CLKDEV_CON_ID("vdc3", &mstp_clks[MSTP74]),
	CLKDEV_ICK_ID("fck", "sh-cmt-16.0", &mstp_clks[MSTP72]),
	CLKDEV_CON_ID("usb0", &mstp_clks[MSTP60]),
	CLKDEV_CON_ID("mtu2_fck", &mstp_clks[MSTP35]),
	CLKDEV_CON_ID("sdhi0", &mstp_clks[MSTP34]),
	CLKDEV_CON_ID("sdhi1", &mstp_clks[MSTP33]),
	CLKDEV_CON_ID("adc0", &mstp_clks[MSTP32]),
	CLKDEV_CON_ID("rtc0", &mstp_clks[MSTP30]),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	if (test_mode_pin(MODE_PIN0)) {
		if (test_mode_pin(MODE_PIN1))
			pll1_div = 3;
		else
			pll1_div = 4;
	} else
		pll1_div = 1;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	return ret;
}
