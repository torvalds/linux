/*
 * SH7372 clock framework support
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

/* SH7372 registers */
#define FRQCRA		0xe6150000
#define FRQCRB		0xe6150004
#define FRQCRC		0xe61500e0
#define FRQCRD		0xe61500e4
#define VCLKCR1		0xe6150008
#define VCLKCR2		0xe615000c
#define VCLKCR3		0xe615001c
#define FMSICKCR	0xe6150010
#define FMSOCKCR	0xe6150014
#define FSIACKCR	0xe6150018
#define FSIBCKCR	0xe6150090
#define SUBCKCR		0xe6150080
#define SPUCKCR		0xe6150084
#define VOUCKCR		0xe6150088
#define HDMICKCR	0xe6150094
#define DSITCKCR	0xe6150060
#define DSI0PCKCR	0xe6150064
#define DSI1PCKCR	0xe6150098
#define PLLC01CR	0xe6150028
#define PLLC2CR		0xe615002c
#define RMSTPCR0	0xe6150110
#define RMSTPCR1	0xe6150114
#define RMSTPCR2	0xe6150118
#define RMSTPCR3	0xe615011c
#define RMSTPCR4	0xe6150120
#define SMSTPCR0	0xe6150130
#define SMSTPCR1	0xe6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xe615013c
#define SMSTPCR4	0xe6150140

#define FSIDIVA		0xFE1F8000
#define FSIDIVB		0xFE1F8008

/* Platforms must set frequency on their DV_CLKI pin */
struct clk sh7372_dv_clki_clk = {
};

/* Fixed 32 KHz root clock from EXTALR pin */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * 26MHz default rate for the EXTAL1 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh7372_extal1_clk = {
	.rate		= 26000000,
};

/*
 * 48MHz default rate for the EXTAL2 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
struct clk sh7372_extal2_clk = {
	.rate		= 48000000,
};

/* A fixed divide-by-2 block */
static unsigned long div2_recalc(struct clk *clk)
{
	return clk->parent->rate / 2;
}

static struct clk_ops div2_clk_ops = {
	.recalc		= div2_recalc,
};

/* Divide dv_clki by two */
struct clk sh7372_dv_clki_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh7372_dv_clki_clk,
};

/* Divide extal1 by two */
static struct clk extal1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh7372_extal1_clk,
};

/* Divide extal2 by two */
static struct clk extal2_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &sh7372_extal2_clk,
};

/* Divide extal2 by four */
static struct clk extal2_div4_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &extal2_div2_clk,
};

/* PLLC0 and PLLC1 */
static unsigned long pllc01_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLC01CR) & (1 << 14))
		mult = (((__raw_readl(clk->enable_reg) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
}

static struct clk_ops pllc01_clk_ops = {
	.recalc		= pllc01_recalc,
};

static struct clk pllc0_clk = {
	.ops		= &pllc01_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &extal1_div2_clk,
	.enable_reg	= (void __iomem *)FRQCRC,
};

static struct clk pllc1_clk = {
	.ops		= &pllc01_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &extal1_div2_clk,
	.enable_reg	= (void __iomem *)FRQCRA,
};

/* Divide PLLC1 by two */
static struct clk pllc1_div2_clk = {
	.ops		= &div2_clk_ops,
	.parent		= &pllc1_clk,
};

/* PLLC2 */

/* Indices are important - they are the actual src selecting values */
static struct clk *pllc2_parent[] = {
	[0] = &extal1_div2_clk,
	[1] = &extal2_div2_clk,
	[2] = &sh7372_dv_clki_div2_clk,
};

/* Only multipliers 20 * 2 to 46 * 2 are valid, last entry for CPUFREQ_TABLE_END */
static struct cpufreq_frequency_table pllc2_freq_table[29];

static void pllc2_table_rebuild(struct clk *clk)
{
	int i;

	/* Initialise PLLC2 frequency table */
	for (i = 0; i < ARRAY_SIZE(pllc2_freq_table) - 2; i++) {
		pllc2_freq_table[i].frequency = clk->parent->rate * (i + 20) * 2;
		pllc2_freq_table[i].index = i;
	}

	/* This is a special entry - switching PLL off makes it a repeater */
	pllc2_freq_table[i].frequency = clk->parent->rate;
	pllc2_freq_table[i].index = i;

	pllc2_freq_table[++i].frequency = CPUFREQ_TABLE_END;
	pllc2_freq_table[i].index = i;
}

static unsigned long pllc2_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	pllc2_table_rebuild(clk);

	/*
	 * If the PLL is off, mult == 1, clk->rate will be updated in
	 * pllc2_enable().
	 */
	if (__raw_readl(PLLC2CR) & (1 << 31))
		mult = (((__raw_readl(PLLC2CR) >> 24) & 0x3f) + 1) * 2;

	return clk->parent->rate * mult;
}

static long pllc2_round_rate(struct clk *clk, unsigned long rate)
{
	return clk_rate_table_round(clk, clk->freq_table, rate);
}

static int pllc2_enable(struct clk *clk)
{
	int i;

	__raw_writel(__raw_readl(PLLC2CR) | 0x80000000, PLLC2CR);

	for (i = 0; i < 100; i++)
		if (__raw_readl(PLLC2CR) & 0x80000000) {
			clk->rate = pllc2_recalc(clk);
			return 0;
		}

	pr_err("%s(): timeout!\n", __func__);

	return -ETIMEDOUT;
}

static void pllc2_disable(struct clk *clk)
{
	__raw_writel(__raw_readl(PLLC2CR) & ~0x80000000, PLLC2CR);
}

static int pllc2_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long value;
	int idx;

	idx = clk_rate_table_find(clk, clk->freq_table, rate);
	if (idx < 0)
		return idx;

	if (rate == clk->parent->rate)
		return -EINVAL;

	value = __raw_readl(PLLC2CR) & ~(0x3f << 24);

	__raw_writel(value | ((idx + 19) << 24), PLLC2CR);

	clk->rate = clk->freq_table[idx].frequency;

	return 0;
}

static int pllc2_set_parent(struct clk *clk, struct clk *parent)
{
	u32 value;
	int ret, i;

	if (!clk->parent_table || !clk->parent_num)
		return -EINVAL;

	/* Search the parent */
	for (i = 0; i < clk->parent_num; i++)
		if (clk->parent_table[i] == parent)
			break;

	if (i == clk->parent_num)
		return -ENODEV;

	ret = clk_reparent(clk, parent);
	if (ret < 0)
		return ret;

	value = __raw_readl(PLLC2CR) & ~(3 << 6);

	__raw_writel(value | (i << 6), PLLC2CR);

	/* Rebiuld the frequency table */
	pllc2_table_rebuild(clk);

	return 0;
}

static struct clk_ops pllc2_clk_ops = {
	.recalc		= pllc2_recalc,
	.round_rate	= pllc2_round_rate,
	.set_rate	= pllc2_set_rate,
	.enable		= pllc2_enable,
	.disable	= pllc2_disable,
	.set_parent	= pllc2_set_parent,
};

struct clk sh7372_pllc2_clk = {
	.ops		= &pllc2_clk_ops,
	.parent		= &extal1_div2_clk,
	.freq_table	= pllc2_freq_table,
	.nr_freqs	= ARRAY_SIZE(pllc2_freq_table) - 1,
	.parent_table	= pllc2_parent,
	.parent_num	= ARRAY_SIZE(pllc2_parent),
};

/* External input clock (pin name: FSIACK/FSIBCK ) */
struct clk sh7372_fsiack_clk = {
};

struct clk sh7372_fsibck_clk = {
};

static struct clk *main_clks[] = {
	&sh7372_dv_clki_clk,
	&r_clk,
	&sh7372_extal1_clk,
	&sh7372_extal2_clk,
	&sh7372_dv_clki_div2_clk,
	&extal1_div2_clk,
	&extal2_div2_clk,
	&extal2_div4_clk,
	&pllc0_clk,
	&pllc1_clk,
	&pllc1_div2_clk,
	&sh7372_pllc2_clk,
	&sh7372_fsiack_clk,
	&sh7372_fsibck_clk,
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
			  24, 32, 36, 48, 0, 72, 96, 0 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
	.kick = div4_kick,
};

enum { DIV4_I, DIV4_ZG, DIV4_B, DIV4_M1, DIV4_CSIR,
       DIV4_ZTR, DIV4_ZT, DIV4_ZX, DIV4_HP,
       DIV4_ISPB, DIV4_S, DIV4_ZB, DIV4_ZB3, DIV4_CP,
       DIV4_DDRP, DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
  SH_CLK_DIV4(&pllc1_clk, _reg, _bit, _mask, _flags)

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_I] = DIV4(FRQCRA, 20, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_ZG] = DIV4(FRQCRA, 16, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(FRQCRA, 8, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_M1] = DIV4(FRQCRA, 4, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_CSIR] = DIV4(FRQCRA, 0, 0x6fff, 0),
	[DIV4_ZTR] = DIV4(FRQCRB, 20, 0x6fff, 0),
	[DIV4_ZT] = DIV4(FRQCRB, 16, 0x6fff, 0),
	[DIV4_ZX] = DIV4(FRQCRB, 12, 0x6fff, 0),
	[DIV4_HP] = DIV4(FRQCRB, 4, 0x6fff, 0),
	[DIV4_ISPB] = DIV4(FRQCRC, 20, 0x6fff, 0),
	[DIV4_S] = DIV4(FRQCRC, 12, 0x6fff, 0),
	[DIV4_ZB] = DIV4(FRQCRC, 8, 0x6fff, 0),
	[DIV4_ZB3] = DIV4(FRQCRC, 4, 0x6fff, 0),
	[DIV4_CP] = DIV4(FRQCRC, 0, 0x6fff, 0),
	[DIV4_DDRP] = DIV4(FRQCRD, 0, 0x677c, 0),
};

enum { DIV6_VCK1, DIV6_VCK2, DIV6_VCK3, DIV6_FMSI, DIV6_FMSO,
       DIV6_SUB, DIV6_SPU,
       DIV6_VOU, DIV6_DSIT, DIV6_DSI0P, DIV6_DSI1P,
       DIV6_NR };

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_VCK1] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR1, 0),
	[DIV6_VCK2] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR2, 0),
	[DIV6_VCK3] = SH_CLK_DIV6(&pllc1_div2_clk, VCLKCR3, 0),
	[DIV6_FMSI] = SH_CLK_DIV6(&pllc1_div2_clk, FMSICKCR, 0),
	[DIV6_FMSO] = SH_CLK_DIV6(&pllc1_div2_clk, FMSOCKCR, 0),
	[DIV6_SUB] = SH_CLK_DIV6(&sh7372_extal2_clk, SUBCKCR, 0),
	[DIV6_SPU] = SH_CLK_DIV6(&pllc1_div2_clk, SPUCKCR, 0),
	[DIV6_VOU] = SH_CLK_DIV6(&pllc1_div2_clk, VOUCKCR, 0),
	[DIV6_DSIT] = SH_CLK_DIV6(&pllc1_div2_clk, DSITCKCR, 0),
	[DIV6_DSI0P] = SH_CLK_DIV6(&pllc1_div2_clk, DSI0PCKCR, 0),
	[DIV6_DSI1P] = SH_CLK_DIV6(&pllc1_div2_clk, DSI1PCKCR, 0),
};

enum { DIV6_HDMI, DIV6_FSIA, DIV6_FSIB, DIV6_REPARENT_NR };

/* Indices are important - they are the actual src selecting values */
static struct clk *hdmi_parent[] = {
	[0] = &pllc1_div2_clk,
	[1] = &sh7372_pllc2_clk,
	[2] = &sh7372_dv_clki_clk,
	[3] = NULL,	/* pllc2_div4 not implemented yet */
};

static struct clk *fsiackcr_parent[] = {
	[0] = &pllc1_div2_clk,
	[1] = &sh7372_pllc2_clk,
	[2] = &sh7372_fsiack_clk, /* external input for FSI A */
	[3] = NULL,	/* setting prohibited */
};

static struct clk *fsibckcr_parent[] = {
	[0] = &pllc1_div2_clk,
	[1] = &sh7372_pllc2_clk,
	[2] = &sh7372_fsibck_clk, /* external input for FSI B */
	[3] = NULL,	/* setting prohibited */
};

static struct clk div6_reparent_clks[DIV6_REPARENT_NR] = {
	[DIV6_HDMI] = SH_CLK_DIV6_EXT(&pllc1_div2_clk, HDMICKCR, 0,
				      hdmi_parent, ARRAY_SIZE(hdmi_parent), 6, 2),
	[DIV6_FSIA] = SH_CLK_DIV6_EXT(&pllc1_div2_clk, FSIACKCR, 0,
				      fsiackcr_parent, ARRAY_SIZE(fsiackcr_parent), 6, 2),
	[DIV6_FSIB] = SH_CLK_DIV6_EXT(&pllc1_div2_clk, FSIBCKCR, 0,
				      fsibckcr_parent, ARRAY_SIZE(fsibckcr_parent), 6, 2),
};

/* FSI DIV */
static unsigned long fsidiv_recalc(struct clk *clk)
{
	unsigned long value;

	value = __raw_readl(clk->mapping->base);

	value >>= 16;
	if (value < 2)
		return 0;

	return clk->parent->rate / value;
}

static long fsidiv_round_rate(struct clk *clk, unsigned long rate)
{
	return clk_rate_div_range_round(clk, 2, 0xffff, rate);
}

static void fsidiv_disable(struct clk *clk)
{
	__raw_writel(0, clk->mapping->base);
}

static int fsidiv_enable(struct clk *clk)
{
	unsigned long value;

	value  = __raw_readl(clk->mapping->base) >> 16;
	if (value < 2)
		return -EIO;

	__raw_writel((value << 16) | 0x3, clk->mapping->base);

	return 0;
}

static int fsidiv_set_rate(struct clk *clk, unsigned long rate)
{
	int idx;

	idx = (clk->parent->rate / rate) & 0xffff;
	if (idx < 2)
		return -EINVAL;

	__raw_writel(idx << 16, clk->mapping->base);
	return 0;
}

static struct clk_ops fsidiv_clk_ops = {
	.recalc		= fsidiv_recalc,
	.round_rate	= fsidiv_round_rate,
	.set_rate	= fsidiv_set_rate,
	.enable		= fsidiv_enable,
	.disable	= fsidiv_disable,
};

static struct clk_mapping fsidiva_clk_mapping = {
	.phys	= FSIDIVA,
	.len	= 8,
};

struct clk sh7372_fsidiva_clk = {
	.ops		= &fsidiv_clk_ops,
	.parent		= &div6_reparent_clks[DIV6_FSIA], /* late install */
	.mapping	= &fsidiva_clk_mapping,
};

static struct clk_mapping fsidivb_clk_mapping = {
	.phys	= FSIDIVB,
	.len	= 8,
};

struct clk sh7372_fsidivb_clk = {
	.ops		= &fsidiv_clk_ops,
	.parent		= &div6_reparent_clks[DIV6_FSIB],  /* late install */
	.mapping	= &fsidivb_clk_mapping,
};

static struct clk *late_main_clks[] = {
	&sh7372_fsidiva_clk,
	&sh7372_fsidivb_clk,
};

enum { MSTP001, MSTP000,
       MSTP131, MSTP130,
       MSTP129, MSTP128, MSTP127, MSTP126, MSTP125,
       MSTP118, MSTP117, MSTP116, MSTP113,
       MSTP106, MSTP101, MSTP100,
       MSTP223,
       MSTP218, MSTP217, MSTP216, MSTP214, MSTP208, MSTP207,
       MSTP206, MSTP205, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,
       MSTP328, MSTP323, MSTP322, MSTP314, MSTP313, MSTP312,
       MSTP423, MSTP415, MSTP413, MSTP411, MSTP410, MSTP407, MSTP406,
       MSTP405, MSTP404, MSTP403, MSTP400,
       MSTP_NR };

#define MSTP(_parent, _reg, _bit, _flags) \
  SH_CLK_MSTP32(_parent, _reg, _bit, _flags)

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP001] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR0, 1, 0), /* IIC2 */
	[MSTP000] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR0, 0, 0), /* MSIOF0 */
	[MSTP131] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 31, 0), /* VEU3 */
	[MSTP130] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 30, 0), /* VEU2 */
	[MSTP129] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 29, 0), /* VEU1 */
	[MSTP128] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 28, 0), /* VEU0 */
	[MSTP127] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 27, 0), /* CEU */
	[MSTP126] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 26, 0), /* CSI2 */
	[MSTP125] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR1, 25, 0), /* TMU0 */
	[MSTP118] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 18, 0), /* DSITX */
	[MSTP117] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 17, 0), /* LCDC1 */
	[MSTP116] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR1, 16, 0), /* IIC0 */
	[MSTP113] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR1, 13, 0), /* MERAM */
	[MSTP106] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 6, 0), /* JPU */
	[MSTP101] = MSTP(&div4_clks[DIV4_M1], SMSTPCR1, 1, 0), /* VPU */
	[MSTP100] = MSTP(&div4_clks[DIV4_B], SMSTPCR1, 0, 0), /* LCDC0 */
	[MSTP223] = MSTP(&div6_clks[DIV6_SPU], SMSTPCR2, 23, 0), /* SPU2 */
	[MSTP218] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 18, 0), /* DMAC1 */
	[MSTP217] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 17, 0), /* DMAC2 */
	[MSTP216] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 16, 0), /* DMAC3 */
	[MSTP214] = MSTP(&div4_clks[DIV4_HP], SMSTPCR2, 14, 0), /* USBDMAC */
	[MSTP208] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 8, 0), /* MSIOF1 */
	[MSTP207] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 7, 0), /* SCIFA5 */
	[MSTP206] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 6, 0), /* SCIFB */
	[MSTP205] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 5, 0), /* MSIOF2 */
	[MSTP204] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP201] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 1, 0), /* SCIFA3 */
	[MSTP200] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR2, 0, 0), /* SCIFA4 */
	[MSTP328] = MSTP(&div6_clks[DIV6_SPU], SMSTPCR3, 28, 0), /* FSI2 */
	[MSTP323] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 23, 0), /* IIC1 */
	[MSTP322] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR3, 22, 0), /* USB0 */
	[MSTP314] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP313] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP312] = MSTP(&div4_clks[DIV4_HP], SMSTPCR3, 12, 0), /* MMC */
	[MSTP423] = MSTP(&div4_clks[DIV4_B], SMSTPCR4, 23, 0), /* DSITX1 */
	[MSTP415] = MSTP(&div4_clks[DIV4_HP], SMSTPCR4, 15, 0), /* SDHI2 */
	[MSTP413] = MSTP(&pllc1_div2_clk, SMSTPCR4, 13, 0), /* HDMI */
	[MSTP411] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR4, 11, 0), /* IIC3 */
	[MSTP410] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR4, 10, 0), /* IIC4 */
	[MSTP407] = MSTP(&div4_clks[DIV4_HP], SMSTPCR4, 7, 0), /* USB-DMAC1 */
	[MSTP406] = MSTP(&div6_clks[DIV6_SUB], SMSTPCR4, 6, 0), /* USB1 */
	[MSTP405] = MSTP(&r_clk, SMSTPCR4, 5, 0), /* CMT4 */
	[MSTP404] = MSTP(&r_clk, SMSTPCR4, 4, 0), /* CMT3 */
	[MSTP403] = MSTP(&r_clk, SMSTPCR4, 3, 0), /* KEYSC */
	[MSTP400] = MSTP(&r_clk, SMSTPCR4, 0, 0), /* CMT2 */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("dv_clki_div2_clk", &sh7372_dv_clki_div2_clk),
	CLKDEV_CON_ID("r_clk", &r_clk),
	CLKDEV_CON_ID("extal1", &sh7372_extal1_clk),
	CLKDEV_CON_ID("extal2", &sh7372_extal2_clk),
	CLKDEV_CON_ID("extal1_div2_clk", &extal1_div2_clk),
	CLKDEV_CON_ID("extal2_div2_clk", &extal2_div2_clk),
	CLKDEV_CON_ID("extal2_div4_clk", &extal2_div4_clk),
	CLKDEV_CON_ID("pllc0_clk", &pllc0_clk),
	CLKDEV_CON_ID("pllc1_clk", &pllc1_clk),
	CLKDEV_CON_ID("pllc1_div2_clk", &pllc1_div2_clk),
	CLKDEV_CON_ID("pllc2_clk", &sh7372_pllc2_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("i_clk", &div4_clks[DIV4_I]),
	CLKDEV_CON_ID("zg_clk", &div4_clks[DIV4_ZG]),
	CLKDEV_CON_ID("b_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("m1_clk", &div4_clks[DIV4_M1]),
	CLKDEV_CON_ID("csir_clk", &div4_clks[DIV4_CSIR]),
	CLKDEV_CON_ID("ztr_clk", &div4_clks[DIV4_ZTR]),
	CLKDEV_CON_ID("zt_clk", &div4_clks[DIV4_ZT]),
	CLKDEV_CON_ID("zx_clk", &div4_clks[DIV4_ZX]),
	CLKDEV_CON_ID("hp_clk", &div4_clks[DIV4_HP]),
	CLKDEV_CON_ID("ispb_clk", &div4_clks[DIV4_ISPB]),
	CLKDEV_CON_ID("s_clk", &div4_clks[DIV4_S]),
	CLKDEV_CON_ID("zb_clk", &div4_clks[DIV4_ZB]),
	CLKDEV_CON_ID("zb3_clk", &div4_clks[DIV4_ZB3]),
	CLKDEV_CON_ID("cp_clk", &div4_clks[DIV4_CP]),
	CLKDEV_CON_ID("ddrp_clk", &div4_clks[DIV4_DDRP]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("vck1_clk", &div6_clks[DIV6_VCK1]),
	CLKDEV_CON_ID("vck2_clk", &div6_clks[DIV6_VCK2]),
	CLKDEV_CON_ID("vck3_clk", &div6_clks[DIV6_VCK3]),
	CLKDEV_CON_ID("fmsi_clk", &div6_clks[DIV6_FMSI]),
	CLKDEV_CON_ID("fmso_clk", &div6_clks[DIV6_FMSO]),
	CLKDEV_CON_ID("sub_clk", &div6_clks[DIV6_SUB]),
	CLKDEV_CON_ID("spu_clk", &div6_clks[DIV6_SPU]),
	CLKDEV_CON_ID("vou_clk", &div6_clks[DIV6_VOU]),
	CLKDEV_CON_ID("hdmi_clk", &div6_reparent_clks[DIV6_HDMI]),
	CLKDEV_ICK_ID("dsit_clk", "sh-mipi-dsi.0", &div6_clks[DIV6_DSIT]),
	CLKDEV_ICK_ID("dsit_clk", "sh-mipi-dsi.1", &div6_clks[DIV6_DSIT]),
	CLKDEV_ICK_ID("dsi0p_clk", "sh-mipi-dsi.0", &div6_clks[DIV6_DSI0P]),
	CLKDEV_ICK_ID("dsi1p_clk", "sh-mipi-dsi.1", &div6_clks[DIV6_DSI1P]),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("i2c-sh_mobile.2", &mstp_clks[MSTP001]), /* IIC2 */
	CLKDEV_DEV_ID("spi_sh_msiof.0", &mstp_clks[MSTP000]), /* MSIOF0 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.4", &mstp_clks[MSTP131]), /* VEU3 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.3", &mstp_clks[MSTP130]), /* VEU2 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.2", &mstp_clks[MSTP129]), /* VEU1 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.1", &mstp_clks[MSTP128]), /* VEU0 */
	CLKDEV_DEV_ID("sh_mobile_ceu.0", &mstp_clks[MSTP127]), /* CEU */
	CLKDEV_DEV_ID("sh-mobile-csi2.0", &mstp_clks[MSTP126]), /* CSI2 */
	CLKDEV_DEV_ID("sh_tmu.0", &mstp_clks[MSTP125]), /* TMU00 */
	CLKDEV_DEV_ID("sh_tmu.1", &mstp_clks[MSTP125]), /* TMU01 */
	CLKDEV_DEV_ID("sh-mipi-dsi.0", &mstp_clks[MSTP118]), /* DSITX0 */
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.1", &mstp_clks[MSTP117]), /* LCDC1 */
	CLKDEV_DEV_ID("i2c-sh_mobile.0", &mstp_clks[MSTP116]), /* IIC0 */
	CLKDEV_DEV_ID("sh_mobile_meram.0", &mstp_clks[MSTP113]), /* MERAM */
	CLKDEV_DEV_ID("uio_pdrv_genirq.5", &mstp_clks[MSTP106]), /* JPU */
	CLKDEV_DEV_ID("uio_pdrv_genirq.0", &mstp_clks[MSTP101]), /* VPU */
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.0", &mstp_clks[MSTP100]), /* LCDC0 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.6", &mstp_clks[MSTP223]), /* SPU2DSP0 */
	CLKDEV_DEV_ID("uio_pdrv_genirq.7", &mstp_clks[MSTP223]), /* SPU2DSP1 */
	CLKDEV_DEV_ID("sh-dma-engine.0", &mstp_clks[MSTP218]), /* DMAC1 */
	CLKDEV_DEV_ID("sh-dma-engine.1", &mstp_clks[MSTP217]), /* DMAC2 */
	CLKDEV_DEV_ID("sh-dma-engine.2", &mstp_clks[MSTP216]), /* DMAC3 */
	CLKDEV_DEV_ID("sh-dma-engine.3", &mstp_clks[MSTP214]), /* USB-DMAC0 */
	CLKDEV_DEV_ID("spi_sh_msiof.1", &mstp_clks[MSTP208]), /* MSIOF1 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP207]), /* SCIFA5 */
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP206]), /* SCIFB */
	CLKDEV_DEV_ID("spi_sh_msiof.2", &mstp_clks[MSTP205]), /* MSIOF2 */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]), /* SCIFA0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]), /* SCIFA1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP202]), /* SCIFA2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP201]), /* SCIFA3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP200]), /* SCIFA4 */
	CLKDEV_DEV_ID("sh_fsi2", &mstp_clks[MSTP328]), /* FSI2 */
	CLKDEV_DEV_ID("i2c-sh_mobile.1", &mstp_clks[MSTP323]), /* IIC1 */
	CLKDEV_DEV_ID("r8a66597_hcd.0", &mstp_clks[MSTP322]), /* USB0 */
	CLKDEV_DEV_ID("r8a66597_udc.0", &mstp_clks[MSTP322]), /* USB0 */
	CLKDEV_DEV_ID("renesas_usbhs.0", &mstp_clks[MSTP322]), /* USB0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP314]), /* SDHI0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP313]), /* SDHI1 */
	CLKDEV_DEV_ID("sh_mmcif.0", &mstp_clks[MSTP312]), /* MMC */
	CLKDEV_DEV_ID("sh-mipi-dsi.1", &mstp_clks[MSTP423]), /* DSITX1 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP415]), /* SDHI2 */
	CLKDEV_DEV_ID("sh-mobile-hdmi", &mstp_clks[MSTP413]), /* HDMI */
	CLKDEV_DEV_ID("i2c-sh_mobile.3", &mstp_clks[MSTP411]), /* IIC3 */
	CLKDEV_DEV_ID("i2c-sh_mobile.4", &mstp_clks[MSTP410]), /* IIC4 */
	CLKDEV_DEV_ID("sh-dma-engine.4", &mstp_clks[MSTP407]), /* USB-DMAC1 */
	CLKDEV_DEV_ID("r8a66597_hcd.1", &mstp_clks[MSTP406]), /* USB1 */
	CLKDEV_DEV_ID("r8a66597_udc.1", &mstp_clks[MSTP406]), /* USB1 */
	CLKDEV_DEV_ID("renesas_usbhs.1", &mstp_clks[MSTP406]), /* USB1 */
	CLKDEV_DEV_ID("sh_cmt.4", &mstp_clks[MSTP405]), /* CMT4 */
	CLKDEV_DEV_ID("sh_cmt.3", &mstp_clks[MSTP404]), /* CMT3 */
	CLKDEV_DEV_ID("sh_keysc.0", &mstp_clks[MSTP403]), /* KEYSC */
	CLKDEV_DEV_ID("sh_cmt.2", &mstp_clks[MSTP400]), /* CMT2 */

	CLKDEV_ICK_ID("hdmi", "sh_mobile_lcdc_fb.1",
		      &div6_reparent_clks[DIV6_HDMI]),
	CLKDEV_ICK_ID("ick", "sh-mobile-hdmi", &div6_reparent_clks[DIV6_HDMI]),
	CLKDEV_ICK_ID("icka", "sh_fsi2", &div6_reparent_clks[DIV6_FSIA]),
	CLKDEV_ICK_ID("ickb", "sh_fsi2", &div6_reparent_clks[DIV6_FSIB]),
	CLKDEV_ICK_ID("spu2", "sh_fsi2", &mstp_clks[MSTP223]),
};

void __init sh7372_clock_init(void)
{
	int k, ret = 0;

	/* make sure MSTP bits on the RT/SH4AL-DSP side are off */
	__raw_writel(0xe4ef8087, RMSTPCR0);
	__raw_writel(0xffffffff, RMSTPCR1);
	__raw_writel(0x37c7f7ff, RMSTPCR2);
	__raw_writel(0xffffffff, RMSTPCR3);
	__raw_writel(0xffe0fffd, RMSTPCR4);

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_div6_reparent_register(div6_reparent_clks, DIV6_REPARENT_NR);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	for (k = 0; !ret && (k < ARRAY_SIZE(late_main_clks)); k++)
		ret = clk_register(late_main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		clk_init();
	else
		panic("failed to setup sh7372 clocks\n");

}
