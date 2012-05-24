/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7366.c
 *
 * SH7366 clock framework support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>

/* SH7366 registers */
#define FRQCR		0xa4150000
#define VCLKCR		0xa4150004
#define SCLKACR		0xa4150008
#define SCLKBCR		0xa415000c
#define PLLCR		0xa4150024
#define MSTPCR0		0xa4150030
#define MSTPCR1		0xa4150034
#define MSTPCR2		0xa4150038
#define DLLFRQ		0xa4150050

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
struct clk extal_clk = {
	.rate		= 33333333,
};

/* The dll block multiplies the 32khz r_clk, may be used instead of extal */
static unsigned long dll_recalc(struct clk *clk)
{
	unsigned long mult;

	if (__raw_readl(PLLCR) & 0x1000)
		mult = __raw_readl(DLLFRQ);
	else
		mult = 0;

	return clk->parent->rate * mult;
}

static struct sh_clk_ops dll_clk_ops = {
	.recalc		= dll_recalc,
};

static struct clk dll_clk = {
	.ops		= &dll_clk_ops,
	.parent		= &r_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;
	unsigned long div = 1;

	if (__raw_readl(PLLCR) & 0x4000)
		mult = (((__raw_readl(FRQCR) >> 24) & 0x1f) + 1);
	else
		div = 2;

	return (clk->parent->rate * mult) / div;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&dll_clk,
	&pll_clk,
};

static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_B3, DIV4_P,
       DIV4_SIUA, DIV4_SIUB, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCR, 20, 0x1fef, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4(FRQCR, 16, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(FRQCR, 12, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCR, 8, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B3] = DIV4(FRQCR, 4, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4(FRQCR, 0, 0x1fff, 0),
	[DIV4_SIUA] = DIV4(SCLKACR, 0, 0x1fff, 0),
	[DIV4_SIUB] = DIV4(SCLKBCR, 0, 0x1fff, 0),
};

enum { DIV6_V, DIV6_NR };

struct clk div6_clks[DIV6_NR] = {
	[DIV6_V] = SH_CLK_DIV6(&pll_clk, VCLKCR, 0),
};

#define MSTP(_parent, _reg, _bit, _flags) \
  SH_CLK_MSTP32(_parent, _reg, _bit, _flags)

enum { MSTP031, MSTP030, MSTP029, MSTP028, MSTP026,
       MSTP023, MSTP022, MSTP021, MSTP020, MSTP019, MSTP018, MSTP017, MSTP016,
       MSTP015, MSTP014, MSTP013, MSTP012, MSTP011, MSTP010,
       MSTP007, MSTP006, MSTP005, MSTP002, MSTP001,
       MSTP109, MSTP100,
       MSTP227, MSTP226, MSTP224, MSTP223, MSTP222, MSTP218, MSTP217,
       MSTP211, MSTP207, MSTP205, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,
       MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	/* See page 52 of Datasheet V0.40: Overview -> Block Diagram */
	[MSTP031] = MSTP(&div4_clks[DIV4_I], MSTPCR0, 31, CLK_ENABLE_ON_INIT),
	[MSTP030] = MSTP(&div4_clks[DIV4_I], MSTPCR0, 30, CLK_ENABLE_ON_INIT),
	[MSTP029] = MSTP(&div4_clks[DIV4_I], MSTPCR0, 29, CLK_ENABLE_ON_INIT),
	[MSTP028] = MSTP(&div4_clks[DIV4_SH], MSTPCR0, 28, CLK_ENABLE_ON_INIT),
	[MSTP026] = MSTP(&div4_clks[DIV4_B], MSTPCR0, 26, CLK_ENABLE_ON_INIT),
	[MSTP023] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 23, 0),
	[MSTP022] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 22, 0),
	[MSTP021] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 21, 0),
	[MSTP020] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 20, 0),
	[MSTP019] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 19, 0),
	[MSTP017] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 17, 0),
	[MSTP015] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 15, 0),
	[MSTP014] = MSTP(&r_clk, MSTPCR0, 14, 0),
	[MSTP013] = MSTP(&r_clk, MSTPCR0, 13, 0),
	[MSTP011] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 11, 0),
	[MSTP010] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 10, 0),
	[MSTP007] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 7, 0),
	[MSTP006] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 6, 0),
	[MSTP005] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 5, 0),
	[MSTP002] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 2, 0),
	[MSTP001] = MSTP(&div4_clks[DIV4_P], MSTPCR0, 1, 0),

	[MSTP109] = MSTP(&div4_clks[DIV4_P], MSTPCR1, 9, 0),

	[MSTP227] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 27, 0),
	[MSTP226] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 26, 0),
	[MSTP224] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 24, 0),
	[MSTP223] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 23, 0),
	[MSTP222] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 22, 0),
	[MSTP218] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 18, 0),
	[MSTP217] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 17, 0),
	[MSTP211] = MSTP(&div4_clks[DIV4_P], MSTPCR2, 11, 0),
	[MSTP207] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 7, CLK_ENABLE_ON_INIT),
	[MSTP205] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 5, 0),
	[MSTP204] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 4, 0),
	[MSTP203] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 3, 0),
	[MSTP202] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 2, CLK_ENABLE_ON_INIT),
	[MSTP201] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 1, CLK_ENABLE_ON_INIT),
	[MSTP200] = MSTP(&div4_clks[DIV4_B], MSTPCR2, 0, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("dll_clk", &dll_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("umem_clk", &div4_clks[DIV4_U]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("b3_clk", &div4_clks[DIV4_B3]),
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("siua_clk", &div4_clks[DIV4_SIUA]),
	CLKDEV_CON_ID("siub_clk", &div4_clks[DIV4_SIUB]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("video_clk", &div6_clks[DIV6_V]),

	/* MSTP32 clocks */
	CLKDEV_CON_ID("tlb0", &mstp_clks[MSTP031]),
	CLKDEV_CON_ID("ic0", &mstp_clks[MSTP030]),
	CLKDEV_CON_ID("oc0", &mstp_clks[MSTP029]),
	CLKDEV_CON_ID("rsmem0", &mstp_clks[MSTP028]),
	CLKDEV_CON_ID("xymem0", &mstp_clks[MSTP026]),
	CLKDEV_CON_ID("intc3", &mstp_clks[MSTP023]),
	CLKDEV_CON_ID("intc0", &mstp_clks[MSTP022]),
	CLKDEV_CON_ID("dmac0", &mstp_clks[MSTP021]),
	CLKDEV_CON_ID("sh0", &mstp_clks[MSTP020]),
	CLKDEV_CON_ID("hudi0", &mstp_clks[MSTP019]),
	CLKDEV_CON_ID("ubc0", &mstp_clks[MSTP017]),
	CLKDEV_CON_ID("tmu_fck", &mstp_clks[MSTP015]),
	CLKDEV_CON_ID("cmt_fck", &mstp_clks[MSTP014]),
	CLKDEV_CON_ID("rwdt0", &mstp_clks[MSTP013]),
	CLKDEV_CON_ID("mfi0", &mstp_clks[MSTP011]),
	CLKDEV_CON_ID("flctl0", &mstp_clks[MSTP010]),

	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP007]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP006]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP005]),

	CLKDEV_CON_ID("msiof0", &mstp_clks[MSTP002]),
	CLKDEV_CON_ID("sbr0", &mstp_clks[MSTP001]),
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[MSTP109]),
	CLKDEV_CON_ID("icb0", &mstp_clks[MSTP227]),
	CLKDEV_CON_ID("meram0", &mstp_clks[MSTP226]),
	CLKDEV_CON_ID("dacy1", &mstp_clks[MSTP224]),
	CLKDEV_CON_ID("dacy0", &mstp_clks[MSTP223]),
	CLKDEV_CON_ID("tsif0", &mstp_clks[MSTP222]),
	CLKDEV_CON_ID("sdhi0", &mstp_clks[MSTP218]),
	CLKDEV_CON_ID("mmcif0", &mstp_clks[MSTP217]),
	CLKDEV_CON_ID("usbf0", &mstp_clks[MSTP211]),
	CLKDEV_CON_ID("veu1", &mstp_clks[MSTP207]),
	CLKDEV_CON_ID("vou0", &mstp_clks[MSTP205]),
	CLKDEV_CON_ID("beu0", &mstp_clks[MSTP204]),
	CLKDEV_CON_ID("ceu0", &mstp_clks[MSTP203]),
	CLKDEV_CON_ID("veu0", &mstp_clks[MSTP202]),
	CLKDEV_CON_ID("vpu0", &mstp_clks[MSTP201]),
	CLKDEV_CON_ID("lcdc0", &mstp_clks[MSTP200]),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	/* autodetect extal or dll configuration */
	if (__raw_readl(PLLCR) & 0x1000)
		pll_clk.parent = &dll_clk;
	else
		pll_clk.parent = &extal_clk;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	return ret;
}
