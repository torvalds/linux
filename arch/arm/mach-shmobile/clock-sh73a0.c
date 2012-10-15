/*
 * sh73a0 clock framework support
 *
 * Copyright (C) 2010 Magnus Damm
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
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>

#define FRQCRA		IOMEM(0xe6150000)
#define FRQCRB		IOMEM(0xe6150004)
#define FRQCRD		IOMEM(0xe61500e4)
#define VCLKCR1		IOMEM(0xe6150008)
#define VCLKCR2		IOMEM(0xe615000C)
#define VCLKCR3		IOMEM(0xe615001C)
#define ZBCKCR		IOMEM(0xe6150010)
#define FLCKCR		IOMEM(0xe6150014)
#define SD0CKCR		IOMEM(0xe6150074)
#define SD1CKCR		IOMEM(0xe6150078)
#define SD2CKCR		IOMEM(0xe615007C)
#define FSIACKCR	IOMEM(0xe6150018)
#define FSIBCKCR	IOMEM(0xe6150090)
#define SUBCKCR		IOMEM(0xe6150080)
#define SPUACKCR	IOMEM(0xe6150084)
#define SPUVCKCR	IOMEM(0xe6150094)
#define MSUCKCR		IOMEM(0xe6150088)
#define HSICKCR		IOMEM(0xe615008C)
#define MFCK1CR		IOMEM(0xe6150098)
#define MFCK2CR		IOMEM(0xe615009C)
#define DSITCKCR	IOMEM(0xe6150060)
#define DSI0PCKCR	IOMEM(0xe6150064)
#define DSI1PCKCR	IOMEM(0xe6150068)
#define DSI0PHYCR	0xe615006C
#define DSI1PHYCR	0xe6150070
#define PLLECR		IOMEM(0xe61500d0)
#define PLL0CR		IOMEM(0xe61500d8)
#define PLL1CR		IOMEM(0xe6150028)
#define PLL2CR		IOMEM(0xe615002c)
#define PLL3CR		IOMEM(0xe61500dc)
#define SMSTPCR0	IOMEM(0xe6150130)
#define SMSTPCR1	IOMEM(0xe6150134)
#define SMSTPCR2	IOMEM(0xe6150138)
#define SMSTPCR3	IOMEM(0xe615013c)
#define SMSTPCR4	IOMEM(0xe6150140)
#define SMSTPCR5	IOMEM(0xe6150144)
#define CKSCR		IOMEM(0xe61500c0)

/* Fixed 32 KHz root clock from EXTALR pin */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * 26MHz default rate for the EXTAL1 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh73a0_extal1_clk = {
	.rate		= 26000000,
};

/*
 * 48MHz default rate for the EXTAL2 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh73a0_extal2_clk = {
	.rate		= 48000000,
};

/* A fixed divide-by-2 block */
static unsigned long div2_recalc(struct clk *clk)
{
	return clk->parent->rate / 2;
}

static struct sh_clk_ops div2_clk_ops = {
	.recalc		= div2_recalc,
};

static unsigned long div7_recalc(struct clk *clk)
{
	return clk->parent->rate / 7;
}

static struct sh_clk_ops div7_clk_ops = {
	.recalc		= div7_recalc,
};

static unsigned long div13_recalc(struct clk *clk)
{
	return clk->parent->rate / 13;
}

static struct sh_clk_ops div13_clk_ops = {
	.recalc		= div13_recalc,
};

/* Divide extal1 by two */
static struct clk extal1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh73a0_extal1_clk,
};

/* Divide extal2 by two */
static struct clk extal2_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh73a0_extal2_clk,
};

static struct sh_clk_ops main_clk_ops = {
	.recalc		= followparent_recalc,
};

/* Main clock */
static struct clk main_clk = {
	.ops		= &main_clk_ops,
};

/* Divide Main clock by two */
static struct clk main_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &main_clk,
};

/* PLL0, PLL1, PLL2, PLL3 */
static unsigned long pll_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLECR) & (1 << clk->enable_bit)) {
		mult = (((__raw_readl(clk->enable_reg) >> 24) & 0x3f) + 1);
		/* handle CFG bit for PLL1 and PLL2 */
		switch (clk->enable_bit) {
		case 1:
		case 2:
			if (__raw_readl(clk->enable_reg) & (1 << 20))
				mult *= 2;
		}
	}

	return clk->parent->rate * mult;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll0_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &main_clk,
	.enable_reg	= (void __iomem *)PLL0CR,
	.enable_bit	= 0,
};

static struct clk pll1_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &main_clk,
	.enable_reg	= (void __iomem *)PLL1CR,
	.enable_bit	= 1,
};

static struct clk pll2_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &main_clk,
	.enable_reg	= (void __iomem *)PLL2CR,
	.enable_bit	= 2,
};

static struct clk pll3_clk = {
	.ops		= &pll_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &main_clk,
	.enable_reg	= (void __iomem *)PLL3CR,
	.enable_bit	= 3,
};

/* Divide PLL */
static struct clk pll1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &pll1_clk,
};

static struct clk pll1_div7_clk = {
	.ops		= &div7_clk_ops,
	.parent		= &pll1_clk,
};

static struct clk pll1_div13_clk = {
	.ops		= &div13_clk_ops,
	.parent		= &pll1_clk,
};

/* External input clock */
struct clk sh73a0_extcki_clk = {
};

struct clk sh73a0_extalr_clk = {
};

static struct clk *main_clks[] = {
	&r_clk,
	&sh73a0_extal1_clk,
	&sh73a0_extal2_clk,
	&extal1_div2_clk,
	&extal2_div2_clk,
	&main_clk,
	&main_div2_clk,
	&pll0_clk,
	&pll1_clk,
	&pll2_clk,
	&pll3_clk,
	&pll1_div2_clk,
	&pll1_div7_clk,
	&pll1_div13_clk,
	&sh73a0_extcki_clk,
	&sh73a0_extalr_clk,
};

static void div4_kick(struct clk *clk)
{
	unsigned long value;

	/* set KICK bit in FRQCRB to update hardware setting */
	value = __raw_readl(FRQCRB);
	value |= (1 << 31);
	__raw_writel(value, FRQCRB);
}

static int divisors[] = { 2, 3, 4, 6, 8, 12, 16, 18,
			  24, 0, 36, 48, 7 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
	.kick = div4_kick,
};

enum { DIV4_I, DIV4_ZG, DIV4_M3, DIV4_B, DIV4_M1, DIV4_M2,
	DIV4_Z, DIV4_ZTR, DIV4_ZT, DIV4_ZX, DIV4_HP, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
	SH_CLK_DIV4(&pll1_clk, _reg, _bit, _mask, _flags)

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCRA, 20, 0xfff, CLK_ENABLE_ON_INIT),
	[DIV4_ZG] = DIV4(FRQCRA, 16, 0xbff, CLK_ENABLE_ON_INIT),
	[DIV4_M3] = DIV4(FRQCRA, 12, 0xfff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCRA, 8, 0xfff, CLK_ENABLE_ON_INIT),
	[DIV4_M1] = DIV4(FRQCRA, 4, 0xfff, 0),
	[DIV4_M2] = DIV4(FRQCRA, 0, 0xfff, 0),
	[DIV4_Z] = DIV4(FRQCRB, 24, 0xbff, 0),
	[DIV4_ZTR] = DIV4(FRQCRB, 20, 0xfff, 0),
	[DIV4_ZT] = DIV4(FRQCRB, 16, 0xfff, 0),
	[DIV4_ZX] = DIV4(FRQCRB, 12, 0xfff, 0),
	[DIV4_HP] = DIV4(FRQCRB, 4, 0xfff, 0),
};

enum { DIV6_VCK1, DIV6_VCK2, DIV6_VCK3, DIV6_ZB1,
	DIV6_FLCTL, DIV6_SDHI0, DIV6_SDHI1, DIV6_SDHI2,
	DIV6_FSIA, DIV6_FSIB, DIV6_SUB,
	DIV6_SPUA, DIV6_SPUV, DIV6_MSU,
	DIV6_HSI,  DIV6_MFG1, DIV6_MFG2,
	DIV6_DSIT, DIV6_DSI0P, DIV6_DSI1P,
	DIV6_NR };

static struct clk *vck_parent[8] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2_clk,
	[2] = &sh73a0_extcki_clk,
	[3] = &sh73a0_extal2_clk,
	[4] = &main_div2_clk,
	[5] = &sh73a0_extalr_clk,
	[6] = &main_clk,
};

static struct clk *pll_parent[4] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2_clk,
	[2] = &pll1_div13_clk,
};

static struct clk *hsi_parent[4] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2_clk,
	[2] = &pll1_div7_clk,
};

static struct clk *pll_extal2_parent[] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2_clk,
	[2] = &sh73a0_extal2_clk,
	[3] = &sh73a0_extal2_clk,
};

static struct clk *dsi_parent[8] = {
	[0] = &pll1_div2_clk,
	[1] = &pll2_clk,
	[2] = &main_clk,
	[3] = &sh73a0_extal2_clk,
	[4] = &sh73a0_extcki_clk,
};

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_VCK1] = SH_CLK_DIV6_EXT(VCLKCR1, 0,
			vck_parent, ARRAY_SIZE(vck_parent), 12, 3),
	[DIV6_VCK2] = SH_CLK_DIV6_EXT(VCLKCR2, 0,
			vck_parent, ARRAY_SIZE(vck_parent), 12, 3),
	[DIV6_VCK3] = SH_CLK_DIV6_EXT(VCLKCR3, 0,
			vck_parent, ARRAY_SIZE(vck_parent), 12, 3),
	[DIV6_ZB1] = SH_CLK_DIV6_EXT(ZBCKCR, CLK_ENABLE_ON_INIT,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_FLCTL] = SH_CLK_DIV6_EXT(FLCKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_SDHI0] = SH_CLK_DIV6_EXT(SD0CKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 6, 2),
	[DIV6_SDHI1] = SH_CLK_DIV6_EXT(SD1CKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 6, 2),
	[DIV6_SDHI2] = SH_CLK_DIV6_EXT(SD2CKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 6, 2),
	[DIV6_FSIA] = SH_CLK_DIV6_EXT(FSIACKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 6, 1),
	[DIV6_FSIB] = SH_CLK_DIV6_EXT(FSIBCKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 6, 1),
	[DIV6_SUB] = SH_CLK_DIV6_EXT(SUBCKCR, 0,
			pll_extal2_parent, ARRAY_SIZE(pll_extal2_parent), 6, 2),
	[DIV6_SPUA] = SH_CLK_DIV6_EXT(SPUACKCR, 0,
			pll_extal2_parent, ARRAY_SIZE(pll_extal2_parent), 6, 2),
	[DIV6_SPUV] = SH_CLK_DIV6_EXT(SPUVCKCR, 0,
			pll_extal2_parent, ARRAY_SIZE(pll_extal2_parent), 6, 2),
	[DIV6_MSU] = SH_CLK_DIV6_EXT(MSUCKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_HSI] = SH_CLK_DIV6_EXT(HSICKCR, 0,
			hsi_parent, ARRAY_SIZE(hsi_parent), 6, 2),
	[DIV6_MFG1] = SH_CLK_DIV6_EXT(MFCK1CR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_MFG2] = SH_CLK_DIV6_EXT(MFCK2CR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_DSIT] = SH_CLK_DIV6_EXT(DSITCKCR, 0,
			pll_parent, ARRAY_SIZE(pll_parent), 7, 1),
	[DIV6_DSI0P] = SH_CLK_DIV6_EXT(DSI0PCKCR, 0,
			dsi_parent, ARRAY_SIZE(dsi_parent), 12, 3),
	[DIV6_DSI1P] = SH_CLK_DIV6_EXT(DSI1PCKCR, 0,
			dsi_parent, ARRAY_SIZE(dsi_parent), 12, 3),
};

/* DSI DIV */
static unsigned long dsiphy_recalc(struct clk *clk)
{
	u32 value;

	value = __raw_readl(clk->mapping->base);

	/* FIXME */
	if (!(value & 0x000B8000))
		return clk->parent->rate;

	value &= 0x3f;
	value += 1;

	if ((value < 12) ||
	    (value > 33)) {
		pr_err("DSIPHY has wrong value (%d)", value);
		return 0;
	}

	return clk->parent->rate / value;
}

static long dsiphy_round_rate(struct clk *clk, unsigned long rate)
{
	return clk_rate_mult_range_round(clk, 12, 33, rate);
}

static void dsiphy_disable(struct clk *clk)
{
	u32 value;

	value = __raw_readl(clk->mapping->base);
	value &= ~0x000B8000;

	__raw_writel(value , clk->mapping->base);
}

static int dsiphy_enable(struct clk *clk)
{
	u32 value;
	int multi;

	value = __raw_readl(clk->mapping->base);
	multi = (value & 0x3f) + 1;

	if ((multi < 12) || (multi > 33))
		return -EIO;

	__raw_writel(value | 0x000B8000, clk->mapping->base);

	return 0;
}

static int dsiphy_set_rate(struct clk *clk, unsigned long rate)
{
	u32 value;
	int idx;

	idx = rate / clk->parent->rate;
	if ((idx < 12) || (idx > 33))
		return -EINVAL;

	idx += -1;

	value = __raw_readl(clk->mapping->base);
	value = (value & ~0x3f) + idx;

	__raw_writel(value, clk->mapping->base);

	return 0;
}

static struct sh_clk_ops dsiphy_clk_ops = {
	.recalc		= dsiphy_recalc,
	.round_rate	= dsiphy_round_rate,
	.set_rate	= dsiphy_set_rate,
	.enable		= dsiphy_enable,
	.disable	= dsiphy_disable,
};

static struct clk_mapping dsi0phy_clk_mapping = {
	.phys	= DSI0PHYCR,
	.len	= 4,
};

static struct clk_mapping dsi1phy_clk_mapping = {
	.phys	= DSI1PHYCR,
	.len	= 4,
};

static struct clk dsi0phy_clk = {
	.ops		= &dsiphy_clk_ops,
	.parent		= &div6_clks[DIV6_DSI0P], /* late install */
	.mapping	= &dsi0phy_clk_mapping,
};

static struct clk dsi1phy_clk = {
	.ops		= &dsiphy_clk_ops,
	.parent		= &div6_clks[DIV6_DSI1P], /* late install */
	.mapping	= &dsi1phy_clk_mapping,
};

static struct clk *late_main_clks[] = {
	&dsi0phy_clk,
	&dsi1phy_clk,
};

enum { MSTP001,
	MSTP129, MSTP128, MSTP127, MSTP126, MSTP125, MSTP118, MSTP116, MSTP100,
	MSTP219, MSTP218, MSTP217,
	MSTP207, MSTP206, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,
	MSTP331, MSTP329, MSTP328, MSTP325, MSTP323, MSTP322,
	MSTP314, MSTP313, MSTP312, MSTP311,
	MSTP303, MSTP302, MSTP301, MSTP300,
	MSTP411, MSTP410, MSTP403,
	MSTP_NR };

#define MSTP(_parent, _reg, _bit, _flags) \
	SH_CLK_MSTP32(_parent, _reg, _bit, _flags)

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP001] = MSTP(&div4_clks[DIV4_HP], SMSTPCR0, 1, 0), /* IIC2 */
	[MSTP129] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 29, 0), /* CEU1 */
	[MSTP128] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 28, 0), /* CSI2-RX1 */
	[MSTP127] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 27, 0), /* CEU0 */
	[MSTP126] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 26, 0), /* CSI2-RX0 */
	[MSTP125] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR1, 25, 0), /* TMU0 */
	[MSTP118] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 18, 0), /* DSITX0 */
	[MSTP116] = MSTP(&div4_clks[DIV4_HP], SMSTPCR1, 16, 0), /* IIC0 */
	[MSTP100] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 0, 0), /* LCDC0 */
	[MSTP219] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 19, 0), /* SCIFA7 */
	[MSTP218] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 18, 0), /* SY-DMAC */
	[MSTP217] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 17, 0), /* MP-DMAC */
	[MSTP207] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 7, 0), /* SCIFA5 */
	[MSTP206] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 6, 0), /* SCIFB */
	[MSTP204] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP201] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 1, 0), /* SCIFA3 */
	[MSTP200] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 0, 0), /* SCIFA4 */
	[MSTP331] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 31, 0), /* SCIFA6 */
	[MSTP329] = MSTP(&r_clk, SMSTPCR3, 29, 0), /* CMT10 */
	[MSTP328] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 28, 0), /*FSI*/
	[MSTP325] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 25, 0), /* IrDA */
	[MSTP323] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 23, 0), /* IIC1 */
	[MSTP322] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 22, 0), /* USB */
	[MSTP314] = MSTP(&div6_clks[DIV6_SDHI0], SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP313] = MSTP(&div6_clks[DIV6_SDHI1], SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP312] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 12, 0), /* MMCIF0 */
	[MSTP311] = MSTP(&div6_clks[DIV6_SDHI2], SMSTPCR3, 11, 0), /* SDHI2 */
	[MSTP303] = MSTP(&main_div2_clk, SMSTPCR3, 3, 0), /* TPU1 */
	[MSTP302] = MSTP(&main_div2_clk, SMSTPCR3, 2, 0), /* TPU2 */
	[MSTP301] = MSTP(&main_div2_clk, SMSTPCR3, 1, 0), /* TPU3 */
	[MSTP300] = MSTP(&main_div2_clk, SMSTPCR3, 0, 0), /* TPU4 */
	[MSTP411] = MSTP(&div4_clks[DIV4_HP], SMSTPCR4, 11, 0), /* IIC3 */
	[MSTP410] = MSTP(&div4_clks[DIV4_HP], SMSTPCR4, 10, 0), /* IIC4 */
	[MSTP403] = MSTP(&r_clk, SMSTPCR4, 3, 0), /* KEYSC */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("r_clk", &r_clk),

	/* DIV6 clocks */
	CLKDEV_CON_ID("vck1_clk", &div6_clks[DIV6_VCK1]),
	CLKDEV_CON_ID("vck2_clk", &div6_clks[DIV6_VCK2]),
	CLKDEV_CON_ID("vck3_clk", &div6_clks[DIV6_VCK3]),
	CLKDEV_CON_ID("sdhi0_clk", &div6_clks[DIV6_SDHI0]),
	CLKDEV_CON_ID("sdhi1_clk", &div6_clks[DIV6_SDHI1]),
	CLKDEV_CON_ID("sdhi2_clk", &div6_clks[DIV6_SDHI2]),
	CLKDEV_ICK_ID("dsit_clk", "sh-mipi-dsi.0", &div6_clks[DIV6_DSIT]),
	CLKDEV_ICK_ID("dsit_clk", "sh-mipi-dsi.1", &div6_clks[DIV6_DSIT]),
	CLKDEV_ICK_ID("dsip_clk", "sh-mipi-dsi.0", &div6_clks[DIV6_DSI0P]),
	CLKDEV_ICK_ID("dsip_clk", "sh-mipi-dsi.1", &div6_clks[DIV6_DSI1P]),
	CLKDEV_ICK_ID("dsiphy_clk", "sh-mipi-dsi.0", &dsi0phy_clk),
	CLKDEV_ICK_ID("dsiphy_clk", "sh-mipi-dsi.1", &dsi1phy_clk),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("i2c-sh_mobile.2", &mstp_clks[MSTP001]), /* I2C2 */
	CLKDEV_DEV_ID("sh_mobile_ceu.1", &mstp_clks[MSTP129]), /* CEU1 */
	CLKDEV_DEV_ID("sh-mobile-csi2.1", &mstp_clks[MSTP128]), /* CSI2-RX1 */
	CLKDEV_DEV_ID("sh_mobile_ceu.0", &mstp_clks[MSTP127]), /* CEU0 */
	CLKDEV_DEV_ID("sh-mobile-csi2.0", &mstp_clks[MSTP126]), /* CSI2-RX0 */
	CLKDEV_DEV_ID("sh_tmu.0", &mstp_clks[MSTP125]), /* TMU00 */
	CLKDEV_DEV_ID("sh_tmu.1", &mstp_clks[MSTP125]), /* TMU01 */
	CLKDEV_DEV_ID("sh-mipi-dsi.0", &mstp_clks[MSTP118]), /* DSITX */
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[MSTP116]), /* I2C0 */
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.0", &mstp_clks[MSTP100]), /* LCDC0 */
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP219]), /* SCIFA7 */
	CLKDEV_DEV_ID("sh-dma-engine.0", &mstp_clks[MSTP218]), /* SY-DMAC */
	CLKDEV_DEV_ID("sh-dma-engine.1", &mstp_clks[MSTP217]), /* MP-DMAC */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP207]), /* SCIFA5 */
	CLKDEV_DEV_ID("sh-sci.8", &mstp_clks[MSTP206]), /* SCIFB */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]), /* SCIFA0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]), /* SCIFA1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP202]), /* SCIFA2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP201]), /* SCIFA3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP200]), /* SCIFA4 */
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP331]), /* SCIFA6 */
	CLKDEV_DEV_ID("sh_cmt.10", &mstp_clks[MSTP329]), /* CMT10 */
	CLKDEV_DEV_ID("sh_fsi2", &mstp_clks[MSTP328]), /* FSI */
	CLKDEV_DEV_ID("sh_irda.0", &mstp_clks[MSTP325]), /* IrDA */
	CLKDEV_DEV_ID("i2c-sh_mobile.1", &mstp_clks[MSTP323]), /* I2C1 */
	CLKDEV_DEV_ID("renesas_usbhs", &mstp_clks[MSTP322]), /* USB */
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]), /* SDHI0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]), /* SDHI1 */
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP312]), /* MMCIF0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP311]), /* SDHI2 */
	CLKDEV_DEV_ID("leds-renesas-tpu.12", &mstp_clks[MSTP303]), /* TPU1 */
	CLKDEV_DEV_ID("leds-renesas-tpu.21", &mstp_clks[MSTP302]), /* TPU2 */
	CLKDEV_DEV_ID("leds-renesas-tpu.30", &mstp_clks[MSTP301]), /* TPU3 */
	CLKDEV_DEV_ID("leds-renesas-tpu.41", &mstp_clks[MSTP300]), /* TPU4 */
	CLKDEV_DEV_ID("i2c-sh_mobile.3", &mstp_clks[MSTP411]), /* I2C3 */
	CLKDEV_DEV_ID("i2c-sh_mobile.4", &mstp_clks[MSTP410]), /* I2C4 */
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[MSTP403]), /* KEYSC */
};

void __init sh73a0_clock_init(void)
{
	int k, ret = 0;

	/* Set SDHI clocks to a known state */
	__raw_writel(0x108, SD0CKCR);
	__raw_writel(0x108, SD1CKCR);
	__raw_writel(0x108, SD2CKCR);

	/* detect main clock parent */
	switch ((__raw_readl(CKSCR) >> 28) & 0x03) {
	case 0:
		main_clk.parent = &sh73a0_extal1_clk;
		break;
	case 1:
		main_clk.parent = &extal1_div2_clk;
		break;
	case 2:
		main_clk.parent = &sh73a0_extal2_clk;
		break;
	case 3:
		main_clk.parent = &extal2_div2_clk;
		break;
	}

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_reparent_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	for (k = 0; !ret && (k < ARRAY_SIZE(late_main_clks)); k++)
		ret = clk_register(late_main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup sh73a0 clocks\n");
}
