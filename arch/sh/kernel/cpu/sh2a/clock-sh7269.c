/*
 * arch/sh/kernel/cpu/sh2a/clock-sh7269.c
 *
 * SH7269 clock framework support
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

/* SH7269 registers */
#define FRQCR		0xfffe0010
#define STBCR3 		0xfffe0408
#define STBCR4 		0xfffe040c
#define STBCR5 		0xfffe0410
#define STBCR6 		0xfffe0414
#define STBCR7 		0xfffe0418

#define PLL_RATE 20

/* Fixed 32 KHz root clock for RTC */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 13340000,
};

static unsigned long pll_recalc(struct clk *clk)
{
	return clk->parent->rate * PLL_RATE;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long peripheral0_recalc(struct clk *clk)
{
	return clk->parent->rate / 8;
}

static struct sh_clk_ops peripheral0_clk_ops = {
	.recalc		= peripheral0_recalc,
};

static struct clk peripheral0_clk = {
	.ops		= &peripheral0_clk_ops,
	.parent		= &pll_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long peripheral1_recalc(struct clk *clk)
{
	return clk->parent->rate / 4;
}

static struct sh_clk_ops peripheral1_clk_ops = {
	.recalc		= peripheral1_recalc,
};

static struct clk peripheral1_clk = {
	.ops		= &peripheral1_clk_ops,
	.parent		= &pll_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&pll_clk,
	&peripheral0_clk,
	&peripheral1_clk,
};

static int div2[] = { 1, 2, 0, 4 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_B,
       DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

/* The mask field specifies the div2 entries that are valid */
struct clk div4_clks[DIV4_NR] = {
	[DIV4_I]  = DIV4(FRQCR, 8, 0xB, CLK_ENABLE_REG_16BIT
					| CLK_ENABLE_ON_INIT),
	[DIV4_B]  = DIV4(FRQCR, 4, 0xA, CLK_ENABLE_REG_16BIT
					| CLK_ENABLE_ON_INIT),
};

enum { MSTP72,
	MSTP60,
	MSTP47, MSTP46, MSTP45, MSTP44, MSTP43, MSTP42, MSTP41, MSTP40,
	MSTP35, MSTP32, MSTP30,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP72] = SH_CLK_MSTP8(&peripheral0_clk, STBCR7, 2, 0), /* CMT */
	[MSTP60] = SH_CLK_MSTP8(&peripheral1_clk, STBCR6, 0, 0), /* USB */
	[MSTP47] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 7, 0), /* SCIF0 */
	[MSTP46] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 6, 0), /* SCIF1 */
	[MSTP45] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 5, 0), /* SCIF2 */
	[MSTP44] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 4, 0), /* SCIF3 */
	[MSTP43] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 3, 0), /* SCIF4 */
	[MSTP42] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 2, 0), /* SCIF5 */
	[MSTP41] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 1, 0), /* SCIF6 */
	[MSTP40] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 0, 0), /* SCIF7 */
	[MSTP35] = SH_CLK_MSTP8(&peripheral0_clk, STBCR3, 5, 0), /* MTU2 */
	[MSTP32] = SH_CLK_MSTP8(&peripheral1_clk, STBCR3, 2, 0), /* ADC */
	[MSTP30] = SH_CLK_MSTP8(&r_clk, STBCR3, 0, 0), /* RTC */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),
	CLKDEV_CON_ID("peripheral_clk", &peripheral1_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),

	/* MSTP clocks */
	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP47]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP46]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP45]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.3", &mstp_clks[MSTP44]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.4", &mstp_clks[MSTP43]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.5", &mstp_clks[MSTP42]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.6", &mstp_clks[MSTP41]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.7", &mstp_clks[MSTP40]),
	CLKDEV_ICK_ID("fck", "sh-cmt-16.0", &mstp_clks[MSTP72]),
	CLKDEV_CON_ID("usb0", &mstp_clks[MSTP60]),
	CLKDEV_ICK_ID("fck", "sh-mtu2", &mstp_clks[MSTP35]),
	CLKDEV_CON_ID("adc0", &mstp_clks[MSTP32]),
	CLKDEV_CON_ID("rtc0", &mstp_clks[MSTP30]),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	return ret;
}
