/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7723.c
 *
 * SH7723 clock framework support
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
#include <cpu/sh7723.h>

/* SH7723 registers */
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

/* The dll multiplies the 32khz r_clk, may be used instead of extal */
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

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_B3, DIV4_P, DIV4_NR };

#define DIV4(_str, _reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(_str, &pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4("cpu_clk", FRQCR, 20, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4("umem_clk", FRQCR, 16, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4("shyway_clk", FRQCR, 12, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4("bus_clk", FRQCR, 8, 0x0dbf, CLK_ENABLE_ON_INIT),
	[DIV4_B3] = DIV4("b3_clk", FRQCR, 4, 0x0db4, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4("peripheral_clk", FRQCR, 0, 0x0dbf, 0),
};

enum { DIV4_IRDA, DIV4_ENABLE_NR };

struct clk div4_enable_clks[DIV4_ENABLE_NR] = {
	[DIV4_IRDA] = DIV4("irda_clk", IRDACLKCR, 0, 0x0dbf, 0),
};

enum { DIV4_SIUA, DIV4_SIUB, DIV4_REPARENT_NR };

struct clk div4_reparent_clks[DIV4_REPARENT_NR] = {
	[DIV4_SIUA] = DIV4("siua_clk", SCLKACR, 0, 0x0dbf, 0),
	[DIV4_SIUB] = DIV4("siub_clk", SCLKBCR, 0, 0x0dbf, 0),
};
struct clk div6_clks[] = {
	SH_CLK_DIV6("video_clk", &pll_clk, VCLKCR, 0),
};

#define R_CLK (&r_clk)
#define P_CLK (&div4_clks[DIV4_P])
#define B_CLK (&div4_clks[DIV4_B])
#define U_CLK (&div4_clks[DIV4_U])
#define I_CLK (&div4_clks[DIV4_I])
#define SH_CLK (&div4_clks[DIV4_SH])

static struct clk mstp_clks[] = {
	/* See page 60 of Datasheet V1.0: Overview -> Block Diagram */
	SH_HWBLK_CLK("tlb0", -1, I_CLK, HWBLK_TLB, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("ic0", -1, I_CLK, HWBLK_IC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("oc0", -1, I_CLK, HWBLK_OC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("l2c0", -1, SH_CLK, HWBLK_L2C, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("ilmem0", -1, I_CLK, HWBLK_ILMEM, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("fpu0", -1, I_CLK, HWBLK_FPU, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("intc0", -1, I_CLK, HWBLK_INTC, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("dmac0", -1, B_CLK, HWBLK_DMAC0, 0),
	SH_HWBLK_CLK("sh0", -1, SH_CLK, HWBLK_SHYWAY, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("hudi0", -1, P_CLK, HWBLK_HUDI, 0),
	SH_HWBLK_CLK("ubc0", -1, I_CLK, HWBLK_UBC, 0),
	SH_HWBLK_CLK("tmu0", -1, P_CLK, HWBLK_TMU0, 0),
	SH_HWBLK_CLK("cmt0", -1, R_CLK, HWBLK_CMT, 0),
	SH_HWBLK_CLK("rwdt0", -1, R_CLK, HWBLK_RWDT, 0),
	SH_HWBLK_CLK("dmac1", -1, B_CLK, HWBLK_DMAC1, 0),
	SH_HWBLK_CLK("tmu1", -1, P_CLK, HWBLK_TMU1, 0),
	SH_HWBLK_CLK("flctl0", -1, P_CLK, HWBLK_FLCTL, 0),
	SH_HWBLK_CLK("scif0", -1, P_CLK, HWBLK_SCIF0, 0),
	SH_HWBLK_CLK("scif1", -1, P_CLK, HWBLK_SCIF1, 0),
	SH_HWBLK_CLK("scif2", -1, P_CLK, HWBLK_SCIF2, 0),
	SH_HWBLK_CLK("scif3", -1, B_CLK, HWBLK_SCIF3, 0),
	SH_HWBLK_CLK("scif4", -1, B_CLK, HWBLK_SCIF4, 0),
	SH_HWBLK_CLK("scif5", -1, B_CLK, HWBLK_SCIF5, 0),
	SH_HWBLK_CLK("msiof0", -1, B_CLK, HWBLK_MSIOF0, 0),
	SH_HWBLK_CLK("msiof1", -1, B_CLK, HWBLK_MSIOF1, 0),
	SH_HWBLK_CLK("meram0", -1, SH_CLK, HWBLK_MERAM, 0),

	SH_HWBLK_CLK("i2c0", -1, P_CLK, HWBLK_IIC, 0),
	SH_HWBLK_CLK("rtc0", -1, R_CLK, HWBLK_RTC, 0),

	SH_HWBLK_CLK("atapi0", -1, SH_CLK, HWBLK_ATAPI, 0),
	SH_HWBLK_CLK("adc0", -1, P_CLK, HWBLK_ADC, 0),
	SH_HWBLK_CLK("tpu0", -1, B_CLK, HWBLK_TPU, 0),
	SH_HWBLK_CLK("irda0", -1, P_CLK, HWBLK_IRDA, 0),
	SH_HWBLK_CLK("tsif0", -1, B_CLK, HWBLK_TSIF, 0),
	SH_HWBLK_CLK("icb0", -1, B_CLK, HWBLK_ICB, CLK_ENABLE_ON_INIT),
	SH_HWBLK_CLK("sdhi0", -1, B_CLK, HWBLK_SDHI0, 0),
	SH_HWBLK_CLK("sdhi1", -1, B_CLK, HWBLK_SDHI1, 0),
	SH_HWBLK_CLK("keysc0", -1, R_CLK, HWBLK_KEYSC, 0),
	SH_HWBLK_CLK("usb0", -1, B_CLK, HWBLK_USB, 0),
	SH_HWBLK_CLK("2dg0", -1, B_CLK, HWBLK_2DG, 0),
	SH_HWBLK_CLK("siu0", -1, B_CLK, HWBLK_SIU, 0),
	SH_HWBLK_CLK("veu1", -1, B_CLK, HWBLK_VEU2H1, 0),
	SH_HWBLK_CLK("vou0", -1, B_CLK, HWBLK_VOU, 0),
	SH_HWBLK_CLK("beu0", -1, B_CLK, HWBLK_BEU, 0),
	SH_HWBLK_CLK("ceu0", -1, B_CLK, HWBLK_CEU, 0),
	SH_HWBLK_CLK("veu0", -1, B_CLK, HWBLK_VEU2H0, 0),
	SH_HWBLK_CLK("vpu0", -1, B_CLK, HWBLK_VPU, 0),
	SH_HWBLK_CLK("lcdc0", -1, B_CLK, HWBLK_LCDC, 0),
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
		ret = sh_clk_div4_enable_register(div4_enable_clks,
					DIV4_ENABLE_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div4_reparent_register(div4_reparent_clks,
					DIV4_REPARENT_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, ARRAY_SIZE(div6_clks));

	if (!ret)
		ret = sh_hwblk_clk_register(mstp_clks, ARRAY_SIZE(mstp_clks));

	return ret;
}
