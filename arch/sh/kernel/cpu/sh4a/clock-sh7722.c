/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7722.c
 *
 * SH7722 clock framework support
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
#include <asm/clock.h>
#include <asm/hwblk.h>
#include <cpu/sh7722.h>

/* SH7722 registers */
#define FRQCR		0xa4150000
#define VCLKCR		0xa4150004
#define SCLKACR		0xa4150008
#define SCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define DLLFRQ		0xa4150050

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.name           = "rclk",
	.id             = -1,
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
struct clk extal_clk = {
	.name		= "extal",
	.id		= -1,
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

static struct clk_ops dll_clk_ops = {
	.recalc		= dll_recalc,
};

static struct clk dll_clk = {
	.name           = "dll_clk",
	.id             = -1,
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

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.name		= "pll_clk",
	.id		= -1,
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

static struct clk_div_mult_table div4_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_B3, DIV4_P,
       DIV4_SIUA, DIV4_SIUB, DIV4_IRDA, DIV4_NR };

#define DIV4(_str, _reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(_str, &pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4("cpu_clk", FRQCR, 20, 0x1fef, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4("umem_clk", FRQCR, 16, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4("shyway_clk", FRQCR, 12, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4("bus_clk", FRQCR, 8, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_B3] = DIV4("b3_clk", FRQCR, 4, 0x1fff, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4("peripheral_clk", FRQCR, 0, 0x1fff, 0),
	[DIV4_SIUA] = DIV4("siua_clk", SCLKACR, 0, 0x1fff, 0),
	[DIV4_SIUB] = DIV4("siub_clk", SCLKBCR, 0, 0x1fff, 0),
	[DIV4_IRDA] = DIV4("irda_clk", IRDACLKCR, 0, 0x1fff, 0),
};

struct clk div6_clks[] = {
	SH_CLK_DIV6("video_clk", &pll_clk, VCLKCR, 0),
};

#define R_CLK &r_clk
#define P_CLK &div4_clks[DIV4_P]
#define B_CLK &div4_clks[DIV4_B]
#define U_CLK &div4_clks[DIV4_U]

static struct clk mstp_clks[] = {
	SH_HWBLK_CLK("uram0", -1, U_CLK, HWBLK_URAM, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("xymem0", -1, B_CLK, HWBLK_XYMEM, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("tmu0", -1, P_CLK, HWBLK_TMU, 0),
	SH_HWBLK_CLK("cmt0", -1, R_CLK, HWBLK_CMT, 0),
	SH_HWBLK_CLK("rwdt0", -1, R_CLK, HWBLK_RWDT, 0),
	SH_HWBLK_CLK("flctl0", -1, P_CLK, HWBLK_FLCTL, 0),
	SH_HWBLK_CLK("scif0", -1, P_CLK, HWBLK_SCIF0, 0),
	SH_HWBLK_CLK("scif1", -1, P_CLK, HWBLK_SCIF1, 0),
	SH_HWBLK_CLK("scif2", -1, P_CLK, HWBLK_SCIF2, 0),

	SH_HWBLK_CLK("i2c0", -1, P_CLK, HWBLK_IIC, 0),
	SH_HWBLK_CLK("rtc0", -1, R_CLK, HWBLK_RTC, 0),

	SH_HWBLK_CLK("sdhi0", -1, P_CLK, HWBLK_SDHI, 0),
	SH_HWBLK_CLK("keysc0", -1, R_CLK, HWBLK_KEYSC, 0),
	SH_HWBLK_CLK("usbf0", -1, P_CLK, HWBLK_USBF, 0),
	SH_HWBLK_CLK("2dg0", -1, B_CLK, HWBLK_2DG, 0),
	SH_HWBLK_CLK("siu0", -1, B_CLK, HWBLK_SIU, 0),
	SH_HWBLK_CLK("vou0", -1, B_CLK, HWBLK_VOU, 0),
	SH_HWBLK_CLK("jpu0", -1, B_CLK, HWBLK_JPU, 0),
	SH_HWBLK_CLK("beu0", -1, B_CLK, HWBLK_BEU, 0),
	SH_HWBLK_CLK("ceu0", -1, B_CLK, HWBLK_CEU, 0),
	SH_HWBLK_CLK("veu0", -1, B_CLK, HWBLK_VEU, 0),
	SH_HWBLK_CLK("vpu0", -1, B_CLK, HWBLK_VPU, 0),
	SH_HWBLK_CLK("lcdc0", -1, P_CLK, HWBLK_LCDC, 0),
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

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, ARRAY_SIZE(div6_clks));

	if (!ret)
		ret = sh_hwblk_clk_register(mstp_clks, ARRAY_SIZE(mstp_clks));

	return ret;
}
