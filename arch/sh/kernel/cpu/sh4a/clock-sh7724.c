// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7724.c
 *
 * SH7724 clock framework support
 *
 * Copyright (C) 2009 Magnus Damm
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/sh_clk.h>
#include <asm/clock.h>
#include <cpu/sh7724.h>

/* SH7724 registers */
#define FRQCRA		0xa4150000
#define FRQCRB		0xa4150004
#define VCLKCR		0xa4150048
#define FCLKACR		0xa4150008
#define FCLKBCR		0xa415000c
#define IRDACLKCR	0xa4150018
#define PLLCR		0xa4150024
#define MSTPCR0		0xa4150030
#define MSTPCR1		0xa4150034
#define MSTPCR2		0xa4150038
#define SPUCLKCR	0xa415003c
#define FLLFRQ		0xa4150050
#define LSTATS		0xa4150060

/* Fixed 32 KHz root clock for RTC and Power Management purposes */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
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

static struct sh_clk_ops fll_clk_ops = {
	.recalc		= fll_recalc,
};

static struct clk fll_clk = {
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

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
};

/* A fixed divide-by-3 block use by the div6 clocks */
static unsigned long div3_recalc(struct clk *clk)
{
	return clk->parent->rate / 3;
}

static struct sh_clk_ops div3_clk_ops = {
	.recalc		= div3_recalc,
};

static struct clk div3_clk = {
	.ops		= &div3_clk_ops,
	.parent		= &pll_clk,
};

/* External input clock (pin name: FSIMCKA/FSIMCKB/DV_CLKI ) */
struct clk sh7724_fsimcka_clk = {
};

struct clk sh7724_fsimckb_clk = {
};

struct clk sh7724_dv_clki = {
};

static struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&fll_clk,
	&pll_clk,
	&div3_clk,
	&sh7724_fsimcka_clk,
	&sh7724_fsimckb_clk,
	&sh7724_dv_clki,
};

static void div4_kick(struct clk *clk)
{
	unsigned long value;

	/* set KICK bit in FRQCRA to update hardware setting */
	value = __raw_readl(FRQCRA);
	value |= (1 << 31);
	__raw_writel(value, FRQCRA);
}

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 0, 24, 32, 36, 48, 0, 72 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
	.kick = div4_kick,
};

enum { DIV4_I, DIV4_SH, DIV4_B, DIV4_P, DIV4_M1, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCRA, 20, 0x2f7d, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(FRQCRA, 12, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCRA, 8, 0x2f7c, CLK_ENABLE_ON_INIT),
	[DIV4_P] = DIV4(FRQCRA, 0, 0x2f7c, 0),
	[DIV4_M1] = DIV4(FRQCRB, 4, 0x2f7c, CLK_ENABLE_ON_INIT),
};

enum { DIV6_V, DIV6_I, DIV6_S, DIV6_FA, DIV6_FB, DIV6_NR };

/* Indices are important - they are the actual src selecting values */
static struct clk *common_parent[] = {
	[0] = &div3_clk,
	[1] = NULL,
};

static struct clk *vclkcr_parent[8] = {
	[0] = &div3_clk,
	[2] = &sh7724_dv_clki,
	[4] = &extal_clk,
};

static struct clk *fclkacr_parent[] = {
	[0] = &div3_clk,
	[1] = NULL,
	[2] = &sh7724_fsimcka_clk,
	[3] = NULL,
};

static struct clk *fclkbcr_parent[] = {
	[0] = &div3_clk,
	[1] = NULL,
	[2] = &sh7724_fsimckb_clk,
	[3] = NULL,
};

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_V] = SH_CLK_DIV6_EXT(VCLKCR, 0,
			vclkcr_parent, ARRAY_SIZE(vclkcr_parent), 12, 3),
	[DIV6_I] = SH_CLK_DIV6_EXT(IRDACLKCR, 0,
			common_parent, ARRAY_SIZE(common_parent), 6, 1),
	[DIV6_S] = SH_CLK_DIV6_EXT(SPUCLKCR, CLK_ENABLE_ON_INIT,
			common_parent, ARRAY_SIZE(common_parent), 6, 1),
	[DIV6_FA] = SH_CLK_DIV6_EXT(FCLKACR, 0,
				      fclkacr_parent, ARRAY_SIZE(fclkacr_parent), 6, 2),
	[DIV6_FB] = SH_CLK_DIV6_EXT(FCLKBCR, 0,
				      fclkbcr_parent, ARRAY_SIZE(fclkbcr_parent), 6, 2),
};

static struct clk mstp_clks[HWBLK_NR] = {
	[HWBLK_TLB] = SH_CLK_MSTP32(&div4_clks[DIV4_I],	    MSTPCR0, 31, CLK_ENABLE_ON_INIT),
	[HWBLK_IC] = SH_CLK_MSTP32(&div4_clks[DIV4_I],	    MSTPCR0, 30, CLK_ENABLE_ON_INIT),
	[HWBLK_OC] = SH_CLK_MSTP32(&div4_clks[DIV4_I],	    MSTPCR0, 29, CLK_ENABLE_ON_INIT),
	[HWBLK_RSMEM] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 28, CLK_ENABLE_ON_INIT),
	[HWBLK_ILMEM] = SH_CLK_MSTP32(&div4_clks[DIV4_I],   MSTPCR0, 27, CLK_ENABLE_ON_INIT),
	[HWBLK_L2C] = SH_CLK_MSTP32(&div4_clks[DIV4_SH],    MSTPCR0, 26, CLK_ENABLE_ON_INIT),
	[HWBLK_FPU] = SH_CLK_MSTP32(&div4_clks[DIV4_I],	    MSTPCR0, 24, CLK_ENABLE_ON_INIT),
	[HWBLK_INTC] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR0, 22, CLK_ENABLE_ON_INIT),
	[HWBLK_DMAC0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 21, 0),
	[HWBLK_SHYWAY] = SH_CLK_MSTP32(&div4_clks[DIV4_SH], MSTPCR0, 20, CLK_ENABLE_ON_INIT),
	[HWBLK_HUDI] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR0, 19, 0),
	[HWBLK_UBC] = SH_CLK_MSTP32(&div4_clks[DIV4_I],     MSTPCR0, 17, 0),
	[HWBLK_TMU0] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR0, 15, 0),
	[HWBLK_CMT] = SH_CLK_MSTP32(&r_clk,		    MSTPCR0, 14, 0),
	[HWBLK_RWDT] = SH_CLK_MSTP32(&r_clk,		    MSTPCR0, 13, 0),
	[HWBLK_DMAC1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 12, 0),
	[HWBLK_TMU1] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR0, 10, 0),
	[HWBLK_SCIF0] = SH_CLK_MSTP32(&div4_clks[DIV4_P],   MSTPCR0, 9, 0),
	[HWBLK_SCIF1] = SH_CLK_MSTP32(&div4_clks[DIV4_P],   MSTPCR0, 8, 0),
	[HWBLK_SCIF2] = SH_CLK_MSTP32(&div4_clks[DIV4_P],   MSTPCR0, 7, 0),
	[HWBLK_SCIF3] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 6, 0),
	[HWBLK_SCIF4] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 5, 0),
	[HWBLK_SCIF5] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR0, 4, 0),
	[HWBLK_MSIOF0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],  MSTPCR0, 2, 0),
	[HWBLK_MSIOF1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],  MSTPCR0, 1, 0),

	[HWBLK_KEYSC] = SH_CLK_MSTP32(&r_clk,		    MSTPCR1, 12, 0),
	[HWBLK_RTC] = SH_CLK_MSTP32(&r_clk,		    MSTPCR1, 11, 0),
	[HWBLK_IIC0] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR1, 9, 0),
	[HWBLK_IIC1] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR1, 8, 0),

	[HWBLK_MMC] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 29, 0),
	[HWBLK_ETHER] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR2, 28, 0),
	[HWBLK_ATAPI] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR2, 26, 0),
	[HWBLK_TPU] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 25, 0),
	[HWBLK_IRDA] = SH_CLK_MSTP32(&div4_clks[DIV4_P],    MSTPCR2, 24, 0),
	[HWBLK_TSIF] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 22, 0),
	[HWBLK_USB1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 21, 0),
	[HWBLK_USB0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 20, 0),
	[HWBLK_2DG] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 19, 0),
	[HWBLK_SDHI0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR2, 18, 0),
	[HWBLK_SDHI1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],   MSTPCR2, 17, 0),
	[HWBLK_VEU1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 15, 0),
	[HWBLK_CEU1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 13, 0),
	[HWBLK_BEU1] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 12, 0),
	[HWBLK_2DDMAC] = SH_CLK_MSTP32(&div4_clks[DIV4_SH], MSTPCR2, 10, 0),
	[HWBLK_SPU] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 9, 0),
	[HWBLK_JPU] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 6, 0),
	[HWBLK_VOU] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 5, 0),
	[HWBLK_BEU0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 4, 0),
	[HWBLK_CEU0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 3, 0),
	[HWBLK_VEU0] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 2, 0),
	[HWBLK_VPU] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	    MSTPCR2, 1, 0),
	[HWBLK_LCDC] = SH_CLK_MSTP32(&div4_clks[DIV4_B],    MSTPCR2, 0, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("fll_clk", &fll_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),
	CLKDEV_CON_ID("div3_clk", &div3_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("vpu_clk", &div4_clks[DIV4_M1]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("video_clk", &div6_clks[DIV6_V]),
	CLKDEV_CON_ID("fsia_clk", &div6_clks[DIV6_FA]),
	CLKDEV_CON_ID("fsib_clk", &div6_clks[DIV6_FB]),
	CLKDEV_CON_ID("irda_clk", &div6_clks[DIV6_I]),
	CLKDEV_CON_ID("spu_clk", &div6_clks[DIV6_S]),

	/* MSTP clocks */
	CLKDEV_CON_ID("tlb0", &mstp_clks[HWBLK_TLB]),
	CLKDEV_CON_ID("ic0", &mstp_clks[HWBLK_IC]),
	CLKDEV_CON_ID("oc0", &mstp_clks[HWBLK_OC]),
	CLKDEV_CON_ID("rs0", &mstp_clks[HWBLK_RSMEM]),
	CLKDEV_CON_ID("ilmem0", &mstp_clks[HWBLK_ILMEM]),
	CLKDEV_CON_ID("l2c0", &mstp_clks[HWBLK_L2C]),
	CLKDEV_CON_ID("fpu0", &mstp_clks[HWBLK_FPU]),
	CLKDEV_CON_ID("intc0", &mstp_clks[HWBLK_INTC]),
	CLKDEV_DEV_ID("sh-dma-engine.0", &mstp_clks[HWBLK_DMAC0]),
	CLKDEV_CON_ID("sh0", &mstp_clks[HWBLK_SHYWAY]),
	CLKDEV_CON_ID("hudi0", &mstp_clks[HWBLK_HUDI]),
	CLKDEV_CON_ID("ubc0", &mstp_clks[HWBLK_UBC]),

	CLKDEV_ICK_ID("fck", "sh-tmu.0", &mstp_clks[HWBLK_TMU0]),
	CLKDEV_ICK_ID("fck", "sh-tmu.1", &mstp_clks[HWBLK_TMU1]),

	CLKDEV_ICK_ID("fck", "sh-cmt-32.0", &mstp_clks[HWBLK_CMT]),
	CLKDEV_DEV_ID("sh-wdt.0", &mstp_clks[HWBLK_RWDT]),
	CLKDEV_DEV_ID("sh-dma-engine.1", &mstp_clks[HWBLK_DMAC1]),

	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[HWBLK_SCIF0]),
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[HWBLK_SCIF1]),
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[HWBLK_SCIF2]),
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[HWBLK_SCIF3]),
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[HWBLK_SCIF4]),
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[HWBLK_SCIF5]),

	CLKDEV_DEV_ID("spi_sh_msiof.0", &mstp_clks[HWBLK_MSIOF0]),
	CLKDEV_DEV_ID("spi_sh_msiof.1", &mstp_clks[HWBLK_MSIOF1]),
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[HWBLK_KEYSC]),
	CLKDEV_CON_ID("rtc0", &mstp_clks[HWBLK_RTC]),
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[HWBLK_IIC0]),
	CLKDEV_DEV_ID("i2c-sh_mobile.1", &mstp_clks[HWBLK_IIC1]),
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[HWBLK_MMC]),
	CLKDEV_DEV_ID("sh7724-ether.0", &mstp_clks[HWBLK_ETHER]),
	CLKDEV_CON_ID("atapi0", &mstp_clks[HWBLK_ATAPI]),
	CLKDEV_CON_ID("tpu0", &mstp_clks[HWBLK_TPU]),
	CLKDEV_CON_ID("irda0", &mstp_clks[HWBLK_IRDA]),
	CLKDEV_CON_ID("tsif0", &mstp_clks[HWBLK_TSIF]),
	CLKDEV_DEV_ID("renesas_usbhs.1", &mstp_clks[HWBLK_USB1]),
	CLKDEV_DEV_ID("renesas_usbhs.0", &mstp_clks[HWBLK_USB0]),
	CLKDEV_CON_ID("usb1", &mstp_clks[HWBLK_USB1]),
	CLKDEV_CON_ID("usb0", &mstp_clks[HWBLK_USB0]),
	CLKDEV_CON_ID("2dg0", &mstp_clks[HWBLK_2DG]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[HWBLK_SDHI0]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[HWBLK_SDHI1]),
	CLKDEV_CON_ID("veu1", &mstp_clks[HWBLK_VEU1]),
	CLKDEV_DEV_ID("renesas-ceu.1", &mstp_clks[HWBLK_CEU1]),
	CLKDEV_CON_ID("beu1", &mstp_clks[HWBLK_BEU1]),
	CLKDEV_CON_ID("2ddmac0", &mstp_clks[HWBLK_2DDMAC]),
	CLKDEV_DEV_ID("sh_fsi.0", &mstp_clks[HWBLK_SPU]),
	CLKDEV_CON_ID("jpu0", &mstp_clks[HWBLK_JPU]),
	CLKDEV_DEV_ID("sh-vou", &mstp_clks[HWBLK_VOU]),
	CLKDEV_CON_ID("beu0", &mstp_clks[HWBLK_BEU0]),
	CLKDEV_DEV_ID("renesas-ceu.0", &mstp_clks[HWBLK_CEU0]),
	CLKDEV_CON_ID("veu0", &mstp_clks[HWBLK_VEU0]),
	CLKDEV_CON_ID("vpu0", &mstp_clks[HWBLK_VPU]),
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.0", &mstp_clks[HWBLK_LCDC]),
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

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_reparent_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, HWBLK_NR);

	return ret;
}
