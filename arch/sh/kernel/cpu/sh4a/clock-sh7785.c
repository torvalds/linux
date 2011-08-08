/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7785.c
 *
 * SH7785 support for the clock framework
 *
 *  Copyright (C) 2007 - 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/clkdev.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <cpu/sh7785.h>

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 33333333,
};

static unsigned long pll_recalc(struct clk *clk)
{
	int multiplier;

	multiplier = test_mode_pin(MODE_PIN4) ? 36 : 72;

	return clk->parent->rate * multiplier;
}

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk *clks[] = {
	&extal_clk,
	&pll_clk,
};

static unsigned int div2[] = { 1, 2, 4, 6, 8, 12, 16, 18,
			       24, 32, 36, 48 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_DDR, DIV4_GA,
	DIV4_DU, DIV4_P, DIV4_NR };

#define DIV4(_bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, FRQMR1, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_P] = DIV4(0, 0x0f80, 0),
	[DIV4_DU] = DIV4(4, 0x0ff0, 0),
	[DIV4_GA] = DIV4(8, 0x0030, 0),
	[DIV4_DDR] = DIV4(12, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(16, 0x0fe0, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(20, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4(24, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_I] = DIV4(28, 0x000e, CLK_ENABLE_ON_INIT),
};

#define MSTPCR0		0xffc80030
#define MSTPCR1		0xffc80034

enum { MSTP029, MSTP028, MSTP027, MSTP026, MSTP025, MSTP024,
       MSTP021, MSTP020, MSTP017, MSTP016,
       MSTP013, MSTP012, MSTP009, MSTP008, MSTP003, MSTP002,
       MSTP119, MSTP117, MSTP105, MSTP104, MSTP100,
       MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	/* MSTPCR0 */
	[MSTP029] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 29, 0),
	[MSTP028] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 28, 0),
	[MSTP027] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 27, 0),
	[MSTP026] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 26, 0),
	[MSTP025] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 25, 0),
	[MSTP024] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 24, 0),
	[MSTP021] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 21, 0),
	[MSTP020] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 20, 0),
	[MSTP017] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 17, 0),
	[MSTP016] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 16, 0),
	[MSTP013] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 13, 0),
	[MSTP012] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 12, 0),
	[MSTP009] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 9, 0),
	[MSTP008] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 8, 0),
	[MSTP003] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 3, 0),
	[MSTP002] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 2, 0),

	/* MSTPCR1 */
	[MSTP119] = SH_CLK_MSTP32(NULL, MSTPCR1, 19, 0),
	[MSTP117] = SH_CLK_MSTP32(NULL, MSTPCR1, 17, 0),
	[MSTP105] = SH_CLK_MSTP32(NULL, MSTPCR1, 5, 0),
	[MSTP104] = SH_CLK_MSTP32(NULL, MSTPCR1, 4, 0),
	[MSTP100] = SH_CLK_MSTP32(NULL, MSTPCR1, 0, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("du_clk", &div4_clks[DIV4_DU]),
	CLKDEV_CON_ID("ga_clk", &div4_clks[DIV4_GA]),
	CLKDEV_CON_ID("ddr_clk", &div4_clks[DIV4_DDR]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("umem_clk", &div4_clks[DIV4_U]),
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),

	/* MSTP32 clocks */
	CLKDEV_ICK_ID("sci_fck", "sh-sci.5", &mstp_clks[MSTP029]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.4", &mstp_clks[MSTP028]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.3", &mstp_clks[MSTP027]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP026]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP025]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP024]),

	CLKDEV_CON_ID("ssi1_fck", &mstp_clks[MSTP021]),
	CLKDEV_CON_ID("ssi0_fck", &mstp_clks[MSTP020]),
	CLKDEV_CON_ID("hac1_fck", &mstp_clks[MSTP017]),
	CLKDEV_CON_ID("hac0_fck", &mstp_clks[MSTP016]),
	CLKDEV_CON_ID("mmcif_fck", &mstp_clks[MSTP013]),
	CLKDEV_CON_ID("flctl_fck", &mstp_clks[MSTP012]),

	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.0", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.1", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.2", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.3", &mstp_clks[MSTP009]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.4", &mstp_clks[MSTP009]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.5", &mstp_clks[MSTP009]),

	CLKDEV_CON_ID("siof_fck", &mstp_clks[MSTP003]),
	CLKDEV_CON_ID("hspi_fck", &mstp_clks[MSTP002]),
	CLKDEV_CON_ID("hudi_fck", &mstp_clks[MSTP119]),
	CLKDEV_CON_ID("ubc_fck", &mstp_clks[MSTP117]),
	CLKDEV_CON_ID("dmac_11_6_fck", &mstp_clks[MSTP105]),
	CLKDEV_CON_ID("dmac_5_0_fck", &mstp_clks[MSTP104]),
	CLKDEV_CON_ID("gdta_fck", &mstp_clks[MSTP100]),
};

int __init arch_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		ret |= clk_register(clks[i]);
	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, ARRAY_SIZE(div4_clks),
					   &div4_table);
	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	return ret;
}
