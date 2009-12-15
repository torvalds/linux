/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7724.c
 *
 * SH7724 clock framework support
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
#include <cpu/sh7724.h>

/* SH7724 registers */
#define FRQCRA		0xa4150000
#define FRQCRB		0xa4150004
#define VCLKCR		0xa4150048
#define FCLKACR		0xa4150008
#define FCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define SPUCLKCR	0xa415003c
#define FLLFRQ		0xa4150050
#define LSTATS		0xa4150060

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

/* The fll multiplies the 32khz r_clk, may be used instead of extal */
static unsigned long fll_recalc(struct clk *clk)
{
	unsigned long mult = 0;
	unsigned long div = 1;

	if (__raw_readl(PLLCR) & 0x1000)
		mult = __raw_readl(FLLFRQ) & 0x3ff;

	if (__raw_readl(FLLFRQ) & 0x4000)
		div = 2;

	return (clk->parent->rate * mult) / div;
}

static struct clk_ops fll_clk_ops = {
	.recalc		= fll_recalc,
};

static struct clk fll_clk = {
	.name           = "fll_clk",
	.id             = -1,
	.ops		= &fll_clk_ops,
	.parent		= &r_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLCR) & 0x4000)
		mult = (((__raw_readl(FRQCRA) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
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

/* A fixed divide-by-3 block use by the div6 clocks */
static unsigned long div3_recalc(struct clk *clk)
{
	return clk->parent->rate / 3;
}

static struct clk_ops div3_clk_ops = {
	.recalc		= div3_recalc,
};

static struct clk div3_clk = {
	.name		= "div3_clk",
	.id		= -1,
	.ops		= &div3_clk_ops,
	.parent		= &pll_clk,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&fll_clk,
	&pll_clk,
	&div3_clk,
};

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 0, 24, 32, 36, 48, 0, 72 };

static struct clk_div_mult_table div4_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

enum { DIV4_I, DIV4_SH, DIV4_B, DIV4_P, DIV4_M1, DIV4_NR };

#define DIV4(_str, _reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(_str, &pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4("cpu_clk", FRQCRA, 20, 0x2f7d, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4("shyway_clk", FRQCRA, 12, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4("bus_clk", FRQCRA, 8, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4("peripheral_clk", FRQCRA, 0, 0x2f7c, 0),
	[DIV4_M1] = DIV4("vpu_clk", FRQCRB, 4, 0x2f7c, 0),
};

struct clk div6_clks[] = {
	SH_CLK_DIV6("video_clk", &div3_clk, VCLKCR, 0),
	SH_CLK_DIV6("fsia_clk", &div3_clk, FCLKACR, 0),
	SH_CLK_DIV6("fsib_clk", &div3_clk, FCLKBCR, 0),
	SH_CLK_DIV6("irda_clk", &div3_clk, IRDACLKCR, 0),
	SH_CLK_DIV6("spu_clk", &div3_clk, SPUCLKCR, CLK_ENABLE_ON_INIT),
};

#define R_CLK (&r_clk)
#define P_CLK (&div4_clks[DIV4_P])
#define B_CLK (&div4_clks[DIV4_B])
#define I_CLK (&div4_clks[DIV4_I])
#define SH_CLK (&div4_clks[DIV4_SH])

static struct clk mstp_clks[] = {
	SH_HWBLK_CLK("tlb0", -1, I_CLK, HWBLK_TLB, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("ic0", -1, I_CLK, HWBLK_IC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("oc0", -1, I_CLK, HWBLK_OC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("rs0", -1, B_CLK, HWBLK_RSMEM, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("ilmem0", -1, I_CLK, HWBLK_ILMEM, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("l2c0", -1, SH_CLK, HWBLK_L2C, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("fpu0", -1, I_CLK, HWBLK_FPU, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("intc0", -1, P_CLK, HWBLK_INTC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("dmac0", -1, B_CLK, HWBLK_DMAC0, 0),
	SH_HWBLK_CLK("sh0", -1, SH_CLK, HWBLK_SHYWAY, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("hudi0", -1, P_CLK, HWBLK_HUDI, 0),
	SH_HWBLK_CLK("ubc0", -1, I_CLK, HWBLK_UBC, 0),
	SH_HWBLK_CLK("tmu0", -1, P_CLK, HWBLK_TMU0, 0),
	SH_HWBLK_CLK("cmt0", -1, R_CLK, HWBLK_CMT, 0),
	SH_HWBLK_CLK("rwdt0", -1, R_CLK, HWBLK_RWDT, 0),
	SH_HWBLK_CLK("dmac1", -1, B_CLK, HWBLK_DMAC1, 0),
	SH_HWBLK_CLK("tmu1", -1, P_CLK, HWBLK_TMU1, 0),
	SH_HWBLK_CLK("scif0", -1, P_CLK, HWBLK_SCIF0, 0),
	SH_HWBLK_CLK("scif1", -1, P_CLK, HWBLK_SCIF1, 0),
	SH_HWBLK_CLK("scif2", -1, P_CLK, HWBLK_SCIF2, 0),
	SH_HWBLK_CLK("scif3", -1, B_CLK, HWBLK_SCIF3, 0),
	SH_HWBLK_CLK("scif4", -1, B_CLK, HWBLK_SCIF4, 0),
	SH_HWBLK_CLK("scif5", -1, B_CLK, HWBLK_SCIF5, 0),
	SH_HWBLK_CLK("msiof0", -1, B_CLK, HWBLK_MSIOF0, 0),
	SH_HWBLK_CLK("msiof1", -1, B_CLK, HWBLK_MSIOF1, 0),

	SH_HWBLK_CLK("keysc0", -1, R_CLK, HWBLK_KEYSC, 0),
	SH_HWBLK_CLK("rtc0", -1, R_CLK, HWBLK_RTC, 0),
	SH_HWBLK_CLK("i2c0", -1, P_CLK, HWBLK_IIC0, 0),
	SH_HWBLK_CLK("i2c1", -1, P_CLK, HWBLK_IIC1, 0),

	SH_HWBLK_CLK("mmc0", -1, B_CLK, HWBLK_MMC, 0),
	SH_HWBLK_CLK("eth0", -1, B_CLK, HWBLK_ETHER, 0),
	SH_HWBLK_CLK("atapi0", -1, B_CLK, HWBLK_ATAPI, 0),
	SH_HWBLK_CLK("tpu0", -1, B_CLK, HWBLK_TPU, 0),
	SH_HWBLK_CLK("irda0", -1, P_CLK, HWBLK_IRDA, 0),
	SH_HWBLK_CLK("tsif0", -1, B_CLK, HWBLK_TSIF, 0),
	SH_HWBLK_CLK("usb1", -1, B_CLK, HWBLK_USB1, 0),
	SH_HWBLK_CLK("usb0", -1, B_CLK, HWBLK_USB0, 0),
	SH_HWBLK_CLK("2dg0", -1, B_CLK, HWBLK_2DG, 0),
	SH_HWBLK_CLK("sdhi0", -1, B_CLK, HWBLK_SDHI0, 0),
	SH_HWBLK_CLK("sdhi1", -1, B_CLK, HWBLK_SDHI1, 0),
	SH_HWBLK_CLK("veu1", -1, B_CLK, HWBLK_VEU1, 0),
	SH_HWBLK_CLK("ceu1", -1, B_CLK, HWBLK_CEU1, 0),
	SH_HWBLK_CLK("beu1", -1, B_CLK, HWBLK_BEU1, 0),
	SH_HWBLK_CLK("2ddmac0", -1, SH_CLK, HWBLK_2DDMAC, 0),
	SH_HWBLK_CLK("spu0", -1, B_CLK, HWBLK_SPU, 0),
	SH_HWBLK_CLK("jpu0", -1, B_CLK, HWBLK_JPU, 0),
	SH_HWBLK_CLK("vou0", -1, B_CLK, HWBLK_VOU, 0),
	SH_HWBLK_CLK("beu0", -1, B_CLK, HWBLK_BEU0, 0),
	SH_HWBLK_CLK("ceu0", -1, B_CLK, HWBLK_CEU0, 0),
	SH_HWBLK_CLK("veu0", -1, B_CLK, HWBLK_VEU0, 0),
	SH_HWBLK_CLK("vpu0", -1, B_CLK, HWBLK_VPU, 0),
	SH_HWBLK_CLK("lcdc0", -1, B_CLK, HWBLK_LCDC, 0),
};

int __init arch_clk_init(void)
{
	int k, ret = 0;

	/* autodetect extal or fll configuration */
	if (__raw_readl(PLLCR) & 0x1000)
		pll_clk.parent = &fll_clk;
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
