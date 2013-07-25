/*
 * R8A7740 processor support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <mach/clock.h>
#include <mach/common.h>
#include <mach/r8a7740.h>

/*
 *        |  MDx  |  XTAL1/EXTAL1   |  System   | EXTALR |
 *  Clock |-------+-----------------+  clock    | 32.768 |   RCLK
 *  Mode  | 2/1/0 | src         MHz |  source   |  KHz   |  source
 * -------+-------+-----------------+-----------+--------+----------
 *    0   | 0 0 0 | External  20~50 | XTAL1     |    O   |  EXTALR
 *    1   | 0 0 1 | Crystal   20~30 | XTAL1     |    O   |  EXTALR
 *    2   | 0 1 0 | External  40~50 | XTAL1 / 2 |    O   |  EXTALR
 *    3   | 0 1 1 | Crystal   40~50 | XTAL1 / 2 |    O   |  EXTALR
 *    4   | 1 0 0 | External  20~50 | XTAL1     |    x   |  XTAL1 / 1024
 *    5   | 1 0 1 | Crystal   20~30 | XTAL1     |    x   |  XTAL1 / 1024
 *    6   | 1 1 0 | External  40~50 | XTAL1 / 2 |    x   |  XTAL1 / 2048
 *    7   | 1 1 1 | Crystal   40~50 | XTAL1 / 2 |    x   |  XTAL1 / 2048
 */

/* CPG registers */
#define FRQCRA		IOMEM(0xe6150000)
#define FRQCRB		IOMEM(0xe6150004)
#define VCLKCR1		IOMEM(0xE6150008)
#define VCLKCR2		IOMEM(0xE615000c)
#define FRQCRC		IOMEM(0xe61500e0)
#define FSIACKCR	IOMEM(0xe6150018)
#define PLLC01CR	IOMEM(0xe6150028)

#define SUBCKCR		IOMEM(0xe6150080)
#define USBCKCR		IOMEM(0xe615008c)

#define MSTPSR0		IOMEM(0xe6150030)
#define MSTPSR1		IOMEM(0xe6150038)
#define MSTPSR2		IOMEM(0xe6150040)
#define MSTPSR3		IOMEM(0xe6150048)
#define MSTPSR4		IOMEM(0xe615004c)
#define FSIBCKCR	IOMEM(0xe6150090)
#define HDMICKCR	IOMEM(0xe6150094)
#define SMSTPCR0	IOMEM(0xe6150130)
#define SMSTPCR1	IOMEM(0xe6150134)
#define SMSTPCR2	IOMEM(0xe6150138)
#define SMSTPCR3	IOMEM(0xe615013c)
#define SMSTPCR4	IOMEM(0xe6150140)

#define FSIDIVA		IOMEM(0xFE1F8000)
#define FSIDIVB		IOMEM(0xFE1F8008)

/* Fixed 32 KHz root clock from EXTALR pin */
static struct clk extalr_clk = {
	.rate	= 32768,
};

/*
 * 25MHz default rate for the EXTAL1 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
static struct clk extal1_clk = {
	.rate	= 25000000,
};

/*
 * 48MHz default rate for the EXTAL2 root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
static struct clk extal2_clk = {
	.rate	= 48000000,
};

/*
 * 27MHz default rate for the DV_CLKI root input clock.
 * If needed, reset this with clk_set_rate() from the platform code.
 */
static struct clk dv_clk = {
	.rate	= 27000000,
};

SH_CLK_RATIO(div2,	1, 2);
SH_CLK_RATIO(div1k,	1, 1024);

SH_FIXED_RATIO_CLK(extal1_div2_clk,	extal1_clk,		div2);
SH_FIXED_RATIO_CLK(extal1_div1024_clk,	extal1_clk,		div1k);
SH_FIXED_RATIO_CLK(extal1_div2048_clk,	extal1_div2_clk,	div1k);
SH_FIXED_RATIO_CLK(extal2_div2_clk,	extal2_clk,		div2);

static struct sh_clk_ops followparent_clk_ops = {
	.recalc	= followparent_recalc,
};

/* Main clock */
static struct clk system_clk = {
	.ops	= &followparent_clk_ops,
};

SH_FIXED_RATIO_CLK(system_div2_clk, system_clk,	div2);

/* r_clk */
static struct clk r_clk = {
	.ops	= &followparent_clk_ops,
};

/* PLLC0/PLLC1 */
static unsigned long pllc01_recalc(struct clk *clk)
{
	unsigned long mult = 1;

	if (__raw_readl(PLLC01CR) & (1 << 14))
		mult = ((__raw_readl(clk->enable_reg) >> 24) & 0x7f) + 1;

	return clk->parent->rate * mult;
}

static struct sh_clk_ops pllc01_clk_ops = {
	.recalc		= pllc01_recalc,
};

static struct clk pllc0_clk = {
	.ops		= &pllc01_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &system_clk,
	.enable_reg	= (void __iomem *)FRQCRC,
};

static struct clk pllc1_clk = {
	.ops		= &pllc01_clk_ops,
	.flags		= CLK_ENABLE_ON_INIT,
	.parent		= &system_div2_clk,
	.enable_reg	= (void __iomem *)FRQCRA,
};

/* PLLC1 / 2 */
SH_FIXED_RATIO_CLK(pllc1_div2_clk, pllc1_clk, div2);

/* USB clock */
/*
 * USBCKCR is controlling usb24 clock
 * bit[7] : parent clock
 * bit[6] : clock divide rate
 * And this bit[7] is used as a "usb24s" from other devices.
 * (Video clock / Sub clock / SPU clock)
 * You can controll this clock as a below.
 *
 * struct clk *usb24	= clk_get(dev,  "usb24");
 * struct clk *usb24s	= clk_get(NULL, "usb24s");
 * struct clk *system	= clk_get(NULL, "system_clk");
 * int rate = clk_get_rate(system);
 *
 * clk_set_parent(usb24s, system);  // for bit[7]
 * clk_set_rate(usb24, rate / 2);   // for bit[6]
 */
static struct clk *usb24s_parents[] = {
	[0] = &system_clk,
	[1] = &extal2_clk
};

static int usb24s_enable(struct clk *clk)
{
	__raw_writel(__raw_readl(USBCKCR) & ~(1 << 8), USBCKCR);

	return 0;
}

static void usb24s_disable(struct clk *clk)
{
	__raw_writel(__raw_readl(USBCKCR) | (1 << 8), USBCKCR);
}

static int usb24s_set_parent(struct clk *clk, struct clk *parent)
{
	int i, ret;
	u32 val;

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

	val = __raw_readl(USBCKCR);
	val &= ~(1 << 7);
	val |= i << 7;
	__raw_writel(val, USBCKCR);

	return 0;
}

static struct sh_clk_ops usb24s_clk_ops = {
	.recalc		= followparent_recalc,
	.enable		= usb24s_enable,
	.disable	= usb24s_disable,
	.set_parent	= usb24s_set_parent,
};

static struct clk usb24s_clk = {
	.ops		= &usb24s_clk_ops,
	.parent_table	= usb24s_parents,
	.parent_num	= ARRAY_SIZE(usb24s_parents),
	.parent		= &system_clk,
};

static unsigned long usb24_recalc(struct clk *clk)
{
	return clk->parent->rate /
		((__raw_readl(USBCKCR) & (1 << 6)) ? 1 : 2);
};

static int usb24_set_rate(struct clk *clk, unsigned long rate)
{
	u32 val;

	/* closer to which ? parent->rate or parent->rate/2 */
	val = __raw_readl(USBCKCR);
	val &= ~(1 << 6);
	val |= (rate > (clk->parent->rate / 4) * 3) << 6;
	__raw_writel(val, USBCKCR);

	return 0;
}

static struct sh_clk_ops usb24_clk_ops = {
	.recalc		= usb24_recalc,
	.set_rate	= usb24_set_rate,
};

static struct clk usb24_clk = {
	.ops		= &usb24_clk_ops,
	.parent		= &usb24s_clk,
};

/* External FSIACK/FSIBCK clock */
static struct clk fsiack_clk = {
};

static struct clk fsibck_clk = {
};

static struct clk *main_clks[] = {
	&extalr_clk,
	&extal1_clk,
	&extal2_clk,
	&extal1_div2_clk,
	&extal1_div1024_clk,
	&extal1_div2048_clk,
	&extal2_div2_clk,
	&dv_clk,
	&system_clk,
	&system_div2_clk,
	&r_clk,
	&pllc0_clk,
	&pllc1_clk,
	&pllc1_div2_clk,
	&usb24s_clk,
	&usb24_clk,
	&fsiack_clk,
	&fsibck_clk,
};

/* DIV4 clocks */
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

enum {
	DIV4_I, DIV4_ZG, DIV4_B, DIV4_M1, DIV4_HP,
	DIV4_HPP, DIV4_USBP, DIV4_S, DIV4_ZB, DIV4_M3, DIV4_CP,
	DIV4_NR
};

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_I]	= SH_CLK_DIV4(&pllc1_clk, FRQCRA, 20, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_ZG]	= SH_CLK_DIV4(&pllc1_clk, FRQCRA, 16, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_B]	= SH_CLK_DIV4(&pllc1_clk, FRQCRA,  8, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_M1]	= SH_CLK_DIV4(&pllc1_clk, FRQCRA,  4, 0x6fff, CLK_ENABLE_ON_INIT),
	[DIV4_HP]	= SH_CLK_DIV4(&pllc1_clk, FRQCRB,  4, 0x6fff, 0),
	[DIV4_HPP]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC, 20, 0x6fff, 0),
	[DIV4_USBP]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC, 16, 0x6fff, 0),
	[DIV4_S]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC, 12, 0x6fff, 0),
	[DIV4_ZB]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC,  8, 0x6fff, 0),
	[DIV4_M3]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC,  4, 0x6fff, 0),
	[DIV4_CP]	= SH_CLK_DIV4(&pllc1_clk, FRQCRC,  0, 0x6fff, 0),
};

/* DIV6 reparent */
enum {
	DIV6_HDMI,
	DIV6_VCLK1, DIV6_VCLK2,
	DIV6_FSIA, DIV6_FSIB,
	DIV6_REPARENT_NR,
};

static struct clk *hdmi_parent[] = {
	[0] = &pllc1_div2_clk,
	[1] = &system_clk,
	[2] = &dv_clk
};

static struct clk *vclk_parents[8] = {
	[0] = &pllc1_div2_clk,
	[2] = &dv_clk,
	[3] = &usb24s_clk,
	[4] = &extal1_div2_clk,
	[5] = &extalr_clk,
};

static struct clk *fsia_parents[] = {
	[0] = &pllc1_div2_clk,
	[1] = &fsiack_clk, /* external clock */
};

static struct clk *fsib_parents[] = {
	[0] = &pllc1_div2_clk,
	[1] = &fsibck_clk, /* external clock */
};

static struct clk div6_reparent_clks[DIV6_REPARENT_NR] = {
	[DIV6_HDMI] = SH_CLK_DIV6_EXT(HDMICKCR, 0,
				      hdmi_parent, ARRAY_SIZE(hdmi_parent), 6, 2),
	[DIV6_VCLK1] = SH_CLK_DIV6_EXT(VCLKCR1, 0,
				       vclk_parents, ARRAY_SIZE(vclk_parents), 12, 3),
	[DIV6_VCLK2] = SH_CLK_DIV6_EXT(VCLKCR2, 0,
				       vclk_parents, ARRAY_SIZE(vclk_parents), 12, 3),
	[DIV6_FSIA] = SH_CLK_DIV6_EXT(FSIACKCR, 0,
				      fsia_parents, ARRAY_SIZE(fsia_parents), 6, 2),
	[DIV6_FSIB] = SH_CLK_DIV6_EXT(FSIBCKCR, 0,
				      fsib_parents, ARRAY_SIZE(fsib_parents), 6, 2),
};

/* DIV6 clocks */
enum {
	DIV6_SUB,
	DIV6_NR
};

static struct clk div6_clks[DIV6_NR] = {
	[DIV6_SUB]	= SH_CLK_DIV6(&pllc1_div2_clk, SUBCKCR, 0),
};

/* HDMI1/2 clock */
static unsigned long hdmi12_recalc(struct clk *clk)
{
	u32 val = __raw_readl(HDMICKCR);
	int shift = (int)clk->priv;

	val >>= shift;
	val &= 0x3;

	return clk->parent->rate / (1 << val);
};

static int hdmi12_set_rate(struct clk *clk, unsigned long rate)
{
	u32 val, mask;
	int i, shift;

	for (i = 0; i < 3; i++)
		if (rate == clk->parent->rate / (1 << i))
			goto find;
	return -ENODEV;

find:
	shift = (int)clk->priv;

	val = __raw_readl(HDMICKCR);
	mask = ~(0x3 << shift);
	val = (val & mask) | i << shift;
	__raw_writel(val, HDMICKCR);

	return 0;
};

static struct sh_clk_ops hdmi12_clk_ops = {
	.recalc		= hdmi12_recalc,
	.set_rate	= hdmi12_set_rate,
};

static struct clk hdmi1_clk = {
	.ops		= &hdmi12_clk_ops,
	.priv		= (void *)9,
	.parent		= &div6_reparent_clks[DIV6_HDMI],  /* late install */
};

static struct clk hdmi2_clk = {
	.ops		= &hdmi12_clk_ops,
	.priv		= (void *)11,
	.parent		= &div6_reparent_clks[DIV6_HDMI], /* late install */
};

static struct clk *late_main_clks[] = {
	&hdmi1_clk,
	&hdmi2_clk,
};

/* FSI DIV */
enum { FSIDIV_A, FSIDIV_B, FSIDIV_REPARENT_NR };

static struct clk fsidivs[] = {
	[FSIDIV_A] = SH_CLK_FSIDIV(FSIDIVA, &div6_reparent_clks[DIV6_FSIA]),
	[FSIDIV_B] = SH_CLK_FSIDIV(FSIDIVB, &div6_reparent_clks[DIV6_FSIB]),
};

/* MSTP */
enum {
	MSTP128, MSTP127, MSTP125,
	MSTP116, MSTP111, MSTP100, MSTP117,

	MSTP230,
	MSTP222,
	MSTP218, MSTP217, MSTP216, MSTP214,
	MSTP207, MSTP206, MSTP204, MSTP203, MSTP202, MSTP201, MSTP200,

	MSTP329, MSTP328, MSTP323, MSTP320,
	MSTP314, MSTP313, MSTP312,
	MSTP309, MSTP304,

	MSTP416, MSTP415, MSTP407, MSTP406,

	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP128] = SH_CLK_MSTP32(&div4_clks[DIV4_S],	SMSTPCR1, 28, 0), /* CEU21 */
	[MSTP127] = SH_CLK_MSTP32(&div4_clks[DIV4_S],	SMSTPCR1, 27, 0), /* CEU20 */
	[MSTP125] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR1, 25, 0), /* TMU0 */
	[MSTP117] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	SMSTPCR1, 17, 0), /* LCDC1 */
	[MSTP116] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR1, 16, 0), /* IIC0 */
	[MSTP111] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR1, 11, 0), /* TMU1 */
	[MSTP100] = SH_CLK_MSTP32(&div4_clks[DIV4_B],	SMSTPCR1,  0, 0), /* LCDC0 */

	[MSTP230] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2, 30, 0), /* SCIFA6 */
	[MSTP222] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2, 22, 0), /* SCIFA7 */
	[MSTP218] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],  SMSTPCR2, 18, 0), /* DMAC1 */
	[MSTP217] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],  SMSTPCR2, 17, 0), /* DMAC2 */
	[MSTP216] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],  SMSTPCR2, 16, 0), /* DMAC3 */
	[MSTP214] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],  SMSTPCR2, 14, 0), /* USBDMAC */
	[MSTP207] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  7, 0), /* SCIFA5 */
	[MSTP206] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  6, 0), /* SCIFB */
	[MSTP204] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  3, 0), /* SCIFA1 */
	[MSTP202] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  2, 0), /* SCIFA2 */
	[MSTP201] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  1, 0), /* SCIFA3 */
	[MSTP200] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR2,  0, 0), /* SCIFA4 */

	[MSTP329] = SH_CLK_MSTP32(&r_clk,		SMSTPCR3, 29, 0), /* CMT10 */
	[MSTP328] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3, 28, 0), /* FSI */
	[MSTP323] = SH_CLK_MSTP32(&div6_clks[DIV6_SUB],	SMSTPCR3, 23, 0), /* IIC1 */
	[MSTP320] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3, 20, 0), /* USBF */
	[MSTP314] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3, 14, 0), /* SDHI0 */
	[MSTP313] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3, 13, 0), /* SDHI1 */
	[MSTP312] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3, 12, 0), /* MMC */
	[MSTP309] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR3,  9, 0), /* GEther */
	[MSTP304] = SH_CLK_MSTP32(&div4_clks[DIV4_CP],	SMSTPCR3,  4, 0), /* TPU0 */

	[MSTP416] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR4, 16, 0), /* USBHOST */
	[MSTP415] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR4, 15, 0), /* SDHI2 */
	[MSTP407] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR4,  7, 0), /* USB-Func */
	[MSTP406] = SH_CLK_MSTP32(&div4_clks[DIV4_HP],	SMSTPCR4,  6, 0), /* USB Phy */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extalr",			&extalr_clk),
	CLKDEV_CON_ID("extal1",			&extal1_clk),
	CLKDEV_CON_ID("extal2",			&extal2_clk),
	CLKDEV_CON_ID("extal1_div2",		&extal1_div2_clk),
	CLKDEV_CON_ID("extal1_div1024",		&extal1_div1024_clk),
	CLKDEV_CON_ID("extal1_div2048",		&extal1_div2048_clk),
	CLKDEV_CON_ID("extal2_div2",		&extal2_div2_clk),
	CLKDEV_CON_ID("dv_clk",			&dv_clk),
	CLKDEV_CON_ID("system_clk",		&system_clk),
	CLKDEV_CON_ID("system_div2_clk",	&system_div2_clk),
	CLKDEV_CON_ID("r_clk",			&r_clk),
	CLKDEV_CON_ID("pllc0_clk",		&pllc0_clk),
	CLKDEV_CON_ID("pllc1_clk",		&pllc1_clk),
	CLKDEV_CON_ID("pllc1_div2_clk",		&pllc1_div2_clk),
	CLKDEV_CON_ID("usb24s",			&usb24s_clk),
	CLKDEV_CON_ID("hdmi1",			&hdmi1_clk),
	CLKDEV_CON_ID("hdmi2",			&hdmi2_clk),
	CLKDEV_CON_ID("video1",			&div6_reparent_clks[DIV6_VCLK1]),
	CLKDEV_CON_ID("video2",			&div6_reparent_clks[DIV6_VCLK2]),
	CLKDEV_CON_ID("fsiack",			&fsiack_clk),
	CLKDEV_CON_ID("fsibck",			&fsibck_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("i_clk",			&div4_clks[DIV4_I]),
	CLKDEV_CON_ID("zg_clk",			&div4_clks[DIV4_ZG]),
	CLKDEV_CON_ID("b_clk",			&div4_clks[DIV4_B]),
	CLKDEV_CON_ID("m1_clk",			&div4_clks[DIV4_M1]),
	CLKDEV_CON_ID("hp_clk",			&div4_clks[DIV4_HP]),
	CLKDEV_CON_ID("hpp_clk",		&div4_clks[DIV4_HPP]),
	CLKDEV_CON_ID("s_clk",			&div4_clks[DIV4_S]),
	CLKDEV_CON_ID("zb_clk",			&div4_clks[DIV4_ZB]),
	CLKDEV_CON_ID("m3_clk",			&div4_clks[DIV4_M3]),
	CLKDEV_CON_ID("cp_clk",			&div4_clks[DIV4_CP]),

	/* DIV6 clocks */
	CLKDEV_CON_ID("sub_clk",		&div6_clks[DIV6_SUB]),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.0",	&mstp_clks[MSTP100]),
	CLKDEV_DEV_ID("sh_tmu.3",		&mstp_clks[MSTP111]),
	CLKDEV_DEV_ID("sh_tmu.4",		&mstp_clks[MSTP111]),
	CLKDEV_DEV_ID("sh_tmu.5",		&mstp_clks[MSTP111]),
	CLKDEV_DEV_ID("i2c-sh_mobile.0",	&mstp_clks[MSTP116]),
	CLKDEV_DEV_ID("fff20000.i2c",		&mstp_clks[MSTP116]),
	CLKDEV_DEV_ID("sh_mobile_lcdc_fb.1",	&mstp_clks[MSTP117]),
	CLKDEV_DEV_ID("sh_tmu.0",		&mstp_clks[MSTP125]),
	CLKDEV_DEV_ID("sh_tmu.1",		&mstp_clks[MSTP125]),
	CLKDEV_DEV_ID("sh_tmu.2",		&mstp_clks[MSTP125]),
	CLKDEV_DEV_ID("sh_mobile_ceu.0",	&mstp_clks[MSTP127]),
	CLKDEV_DEV_ID("sh_mobile_ceu.1",	&mstp_clks[MSTP128]),

	CLKDEV_DEV_ID("sh-sci.4",		&mstp_clks[MSTP200]),
	CLKDEV_DEV_ID("e6c80000.sci",		&mstp_clks[MSTP200]),
	CLKDEV_DEV_ID("sh-sci.3",		&mstp_clks[MSTP201]),
	CLKDEV_DEV_ID("e6c70000.sci",		&mstp_clks[MSTP201]),
	CLKDEV_DEV_ID("sh-sci.2",		&mstp_clks[MSTP202]),
	CLKDEV_DEV_ID("e6c60000.sci",		&mstp_clks[MSTP202]),
	CLKDEV_DEV_ID("sh-sci.1",		&mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("e6c50000.sci",		&mstp_clks[MSTP203]),
	CLKDEV_DEV_ID("sh-sci.0",		&mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("e6c40000.sci",		&mstp_clks[MSTP204]),
	CLKDEV_DEV_ID("sh-sci.8",		&mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("e6c30000.sci",		&mstp_clks[MSTP206]),
	CLKDEV_DEV_ID("sh-sci.5",		&mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("e6cb0000.sci",		&mstp_clks[MSTP207]),
	CLKDEV_DEV_ID("sh-dma-engine.3",	&mstp_clks[MSTP214]),
	CLKDEV_DEV_ID("sh-dma-engine.2",	&mstp_clks[MSTP216]),
	CLKDEV_DEV_ID("sh-dma-engine.1",	&mstp_clks[MSTP217]),
	CLKDEV_DEV_ID("sh-dma-engine.0",	&mstp_clks[MSTP218]),
	CLKDEV_DEV_ID("sh-sci.7",		&mstp_clks[MSTP222]),
	CLKDEV_DEV_ID("e6cd0000.sci",		&mstp_clks[MSTP222]),
	CLKDEV_DEV_ID("sh-sci.6",		&mstp_clks[MSTP230]),
	CLKDEV_DEV_ID("e6cc0000.sci",		&mstp_clks[MSTP230]),

	CLKDEV_DEV_ID("sh_cmt.10",		&mstp_clks[MSTP329]),
	CLKDEV_DEV_ID("sh_fsi2",		&mstp_clks[MSTP328]),
	CLKDEV_DEV_ID("i2c-sh_mobile.1",	&mstp_clks[MSTP323]),
	CLKDEV_DEV_ID("e6c20000.i2c",		&mstp_clks[MSTP323]),
	CLKDEV_DEV_ID("renesas_usbhs",		&mstp_clks[MSTP320]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.0",	&mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("e6850000.sdhi",          &mstp_clks[MSTP314]),
	CLKDEV_DEV_ID("sh_mobile_sdhi.1",	&mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("e6860000.sdhi",          &mstp_clks[MSTP313]),
	CLKDEV_DEV_ID("sh_mmcif",		&mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("e6bd0000.mmcif",         &mstp_clks[MSTP312]),
	CLKDEV_DEV_ID("sh-eth",			&mstp_clks[MSTP309]),
	CLKDEV_DEV_ID("e9a00000.sh-eth",	&mstp_clks[MSTP309]),
	CLKDEV_DEV_ID("renesas-tpu-pwm",	&mstp_clks[MSTP304]),
	CLKDEV_DEV_ID("e6600000.pwm",		&mstp_clks[MSTP304]),

	CLKDEV_DEV_ID("sh_mobile_sdhi.2",	&mstp_clks[MSTP415]),
	CLKDEV_DEV_ID("e6870000.sdhi",          &mstp_clks[MSTP415]),

	/* ICK */
	CLKDEV_ICK_ID("host",	"renesas_usbhs",	&mstp_clks[MSTP416]),
	CLKDEV_ICK_ID("func",	"renesas_usbhs",	&mstp_clks[MSTP407]),
	CLKDEV_ICK_ID("phy",	"renesas_usbhs",	&mstp_clks[MSTP406]),
	CLKDEV_ICK_ID("pci",	"renesas_usbhs",	&div4_clks[DIV4_USBP]),
	CLKDEV_ICK_ID("usb24",	"renesas_usbhs",	&usb24_clk),
	CLKDEV_ICK_ID("ick",	"sh-mobile-hdmi",	&div6_reparent_clks[DIV6_HDMI]),

	CLKDEV_ICK_ID("icka", "sh_fsi2",	&div6_reparent_clks[DIV6_FSIA]),
	CLKDEV_ICK_ID("ickb", "sh_fsi2",	&div6_reparent_clks[DIV6_FSIB]),
	CLKDEV_ICK_ID("diva", "sh_fsi2",	&fsidivs[FSIDIV_A]),
	CLKDEV_ICK_ID("divb", "sh_fsi2",	&fsidivs[FSIDIV_B]),
	CLKDEV_ICK_ID("xcka", "sh_fsi2",	&fsiack_clk),
	CLKDEV_ICK_ID("xckb", "sh_fsi2",	&fsibck_clk),
};

void __init r8a7740_clock_init(u8 md_ck)
{
	int k, ret = 0;

	/* detect system clock parent */
	if (md_ck & MD_CK1)
		system_clk.parent = &extal1_div2_clk;
	else
		system_clk.parent = &extal1_clk;

	/* detect RCLK parent */
	switch (md_ck & (MD_CK2 | MD_CK1)) {
	case MD_CK2 | MD_CK1:
		r_clk.parent = &extal1_div2048_clk;
		break;
	case MD_CK2:
		r_clk.parent = &extal1_div1024_clk;
		break;
	case MD_CK1:
	default:
		r_clk.parent = &extalr_clk;
		break;
	}

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_div6_register(div6_clks, DIV6_NR);

	if (!ret)
		ret = sh_clk_div6_reparent_register(div6_reparent_clks,
						    DIV6_REPARENT_NR);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	for (k = 0; !ret && (k < ARRAY_SIZE(late_main_clks)); k++)
		ret = clk_register(late_main_clks[k]);

	if (!ret)
		ret = sh_clk_fsidiv_register(fsidivs, FSIDIV_REPARENT_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7740 clocks\n");
}
